# kiln

A small build planner written in C++17, with Python tools that check it.

Build systems like `make` and `ninja` are graph programs in disguise. Every target is a
node, every "needs" is an edge, and almost every question you ask about a build is a graph
question: what order do I build in, what is the fastest this can ever finish, what do I
have to redo if this one file changes. kiln reads a plain text description of a project and
answers those questions. It does not run any commands, it only plans and analyses.

## What it can tell you

| command | question it answers |
|---|---|
| `plan` | In what order can things be built, and which targets can run side by side? |
| `critical` | What is the fastest possible build, which chain of targets decides that, and who has slack? |
| `simulate` | How long does the build take with N workers, with a naive scheduler vs a smarter one? |
| `sweep` | How much does adding workers actually help before it stops helping? |
| `impact` | If I change these files, what has to be rebuilt and how long will it take? |
| `hotspots` | Which targets are the most expensive to touch? |

It also gives readable errors: all syntax problems at once with line numbers, and for a
dependency cycle it prints the actual cycle (`a -> b -> c -> a`).

## Input format

```
# name  cost_in_ms : dependencies
config.h     1 :
json.h       4 : common.h
json.o     940 : json.h log.h
server     420 : libcore.a db.o handlers.o
```

Dependencies can be defined later in the file. The colon can be left out for targets with
no dependencies. `examples/webapp.kiln` is a made up 25 target C++ service.

## Build and run

You need a C++17 compiler (developed with g++ 13), `make`, and Python 3.7 or newer for the checking tools. The Python tools use only the standard library.

```
make              # builds build/kiln and build/kiln_tests
make test         # C++ unit tests
make check        # compares kiln with an independent Python implementation
make sanitize     # unit tests again under AddressSanitizer and UBSan
make demo         # runs every command on the example project
```

```
./build/kiln plan      examples/webapp.kiln
./build/kiln critical  examples/webapp.kiln
./build/kiln simulate  examples/webapp.kiln --workers 4 --trace
./build/kiln sweep     examples/webapp.kiln
./build/kiln impact    examples/webapp.kiln --changed json.h,db.h
./build/kiln hotspots  examples/webapp.kiln --top 5
```

To try a big project, generate one: `python3 tools/gen_graph.py --nodes 5000 --layers 30 > big.kiln`

## Example output

```
$ ./build/kiln critical examples/webapp.kiln
total work     : 12199 ms (one worker does everything)
critical path  : 2559 ms (best possible build time, even with endless workers)
best speedup   : 4.77x
7 of 25 targets have zero slack

one critical path (7 targets):
  config.h -> common.h -> json.h -> router.h ->
  handlers.o -> server -> package

$ ./build/kiln simulate examples/webapp.kiln --workers 4
  policy             build time  utilization
  fifo                  4491 ms        67.9%
  critical-first        3209 ms        95.0%

no schedule can beat 3050 ms here (the larger of critical path 2559 ms and work/workers 3050 ms)
critical-first is 5.2% above that limit, fifo is 47.2% above
```

More output is in `examples/sample_output.txt`.

## How it works

**Graph storage** (`models/graph`). Each target keeps two lists: what it depends on, and
what depends on it. The second list costs a little memory but makes "what breaks if this
changes" a simple walk forward instead of a search through everything.

**Build order and levels** (`algorithms/toposort`). Kahn's algorithm. Targets whose
dependencies are all done go into a min-heap keyed by file position, so when there are
several choices the output follows the order you wrote the file in, and the result is the
same on every run. The level of a target is one more than the highest level among its
dependencies. Everything in one level can run in parallel.

**Cycle finder** (`algorithms/toposort`). If Kahn's algorithm leaves targets behind, a
depth first search with three colours (unvisited, on the current path, finished) finds an
actual cycle to show the user. It is written with an explicit stack instead of recursion,
because a chain of a few hundred thousand targets would overflow the call stack. There is a
test with 200,000 targets in a row.

**Critical path and slack** (`algorithms/critical_path`). This is the critical path method
used in project planning. A forward pass over the topological order gives the earliest each
target can finish. A backward pass gives the latest it can start without delaying the whole
build. The gap between the two is the slack. Zero slack means the target is on a critical
path. The longest chain length ("span") is the fastest the build can ever be, and total
work divided by span is the best speedup you can hope for.

**Scheduler simulation** (`services/scheduler`). An event driven simulation with two heaps:
one for ready targets, one for running targets ordered by finish time. When a worker is free
it takes the best ready target. Two policies are compared:

- `fifo`: first come, first served.
- `critical-first`: run the target with the longest chain still ahead of it. That value is
  the "bottom level" computed in the backward pass.

Finding the best schedule is NP-hard, so critical-first is a heuristic, but a good one. The
tests also check a known guarantee: any schedule that never leaves a worker idle while work
is ready finishes within `work/p + (1 - 1/p) * span` (Graham, 1966).

**Impact analysis** (`algorithms/reach`). Breadth first search over the reverse edges from
the changed targets. The time estimate with endless workers reruns the longest path
calculation on only the affected targets.

**Hotspots** (`algorithms/reach`). For every target, count what a change would rebuild, and
keep only the best k in a heap that holds the worst of the current best at its top, so most
candidates are rejected with one comparison. To avoid clearing a visited array for every
start node, the array stores the id of the last start node that touched each entry.

| command | time |
|---|---|
| plan | O((V + E) log V) |
| critical | O(V + E) |
| simulate | O((V + E) log V) |
| impact | O(V + E) |
| cycle search | O(V + E) |
| hotspots | O(V (V + E) + V log k) |

V is the number of targets and E the number of dependencies.

## Testing

- `tests/test_kiln.cpp`: about 160k checks without any test framework. Hand worked
  examples (a diamond graph with known slack, a case where fifo gives 23 and critical-first
  gives 20), edge cases (empty graph, free targets, self loops, CRLF files, a BOM), and 150
  random graphs checked against a brute force version. Also runs clean under ASan and UBSan.
- `tools/verify.py`: a separate Python implementation that reads the same file, recomputes
  everything and compares it with what the C++ program printed. For each schedule it checks
  that every target runs once, dependencies finish before dependants start, no worker
  does two things at once, and the Graham bound holds.
- To make sure the Python checker is not just agreeing with everything, I broke the C++
  code on purpose in six ways (scheduler starts tasks too early, wrong tie break in
  hotspots, off by one span, wrong tie break in topological order, impact forgets the
  changed target, cycle finder drops the last node). The checker caught all six.

## Limits

- `hotspots` is quadratic. Measured on a small single core Linux machine: 0.14 s for 5,000
  targets, 4 s for 20,000, 35 s for 50,000. The other commands take about 0.2 s at 50,000
  targets. A bitset per target (descendants as bits)
  would make hotspots much faster at the price of V squared bits of memory.
- Costs are numbers you provide. kiln does not measure real build times.
- It does not run commands or look at file timestamps or hashes, it only plans.
- All workers are identical and there is no memory limit or I/O contention in the model.
- The heuristic is not optimal. For example critical-first was 5.2% above the lower bound
  in the sample project, and the true optimum is somewhere in between.

## Ideas for later

- Read a real `Makefile` or `build.ninja` instead of the custom format.
- Remember content hashes between runs to find what really changed.
- Try other priority rules (most dependants first, longest own cost first) and compare.
- Let targets need a resource (a lock, a license, memory) so workers cannot run them together.

## Layout

```
include/kiln/models/      graph
include/kiln/algorithms/  toposort, critical_path, reach
include/kiln/services/    parser, scheduler
src/                      implementations, main.cpp is the command line tool
tests/test_kiln.cpp       unit tests
tools/                    gen_graph.py (random projects), verify.py (cross check)
examples/                 webapp.kiln, cycle.kiln, typos.kiln, sample_output.txt
```
