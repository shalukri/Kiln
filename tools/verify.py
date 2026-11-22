#!/usr/bin/env python3
"""Cross-check the C++ kiln binary against a separate Python implementation.

The Python side re-reads the .kiln file on its own, recomputes everything with simple
(slow) code and compares. Nothing here shares code with the C++ project.

    python3 tools/verify.py examples/webapp.kiln
    python3 tools/verify.py broken.kiln --expect-cycle
"""
import argparse
import heapq
import math
import os
import random
import subprocess
import sys
from collections import deque

failures = 0


def check(ok, what):
    global failures
    if not ok:
        failures += 1
        print("    FAIL: " + what)
    return ok


def read_kiln(path):
    names, cost, deps = [], {}, {}
    with open(path, encoding="utf-8-sig") as f:
        for line in f:
            line = line.split("#", 1)[0].strip()
            if not line:
                continue
            head, _, tail = line.partition(":")
            name, c = head.split()
            names.append(name)
            cost[name] = int(c)
            deps[name] = list(dict.fromkeys(tail.split()))
    users = {n: [] for n in names}
    for n in names:
        for d in deps[n]:
            users[d].append(n)
    return names, cost, deps, users


def run_kiln(binary, *args):
    p = subprocess.run([binary, *args], capture_output=True, text=True)
    return p.returncode, p.stdout, p.stderr


def lines_with(out, key):
    return [l.split()[1:] for l in out.splitlines() if l.split() and l.split()[0] == key]


def topo_order(names, deps, users):
    """Kahn with a min-heap on file position, same tie break rule the C++ side documents."""
    pos = {n: i for i, n in enumerate(names)}
    waiting = {n: len(deps[n]) for n in names}
    heap = [pos[n] for n in names if waiting[n] == 0]
    heapq.heapify(heap)
    order = []
    while heap:
        n = names[heapq.heappop(heap)]
        order.append(n)
        for u in users[n]:
            waiting[u] -= 1
            if waiting[u] == 0:
                heapq.heappush(heap, pos[u])
    return order


def span_of(order, cost, deps, mask=None):
    fin = {}
    best = 0
    for n in order:
        if mask is not None and n not in mask:
            continue
        start = max((fin[d] for d in deps[n] if mask is None or d in mask), default=0)
        fin[n] = start + cost[n]
        best = max(best, fin[n])
    return best


def affected(users, changed):
    seen = set(changed)
    q = deque(changed)
    while q:
        for v in users[q.popleft()]:
            if v not in seen:
                seen.add(v)
                q.append(v)
    return seen


# ------------------------------------------------------------------ checks

def check_plan(binary, path, names, cost, deps, users):
    print("  plan: order and levels")
    rc, out, err = run_kiln(binary, "plan", path, "--raw")
    if not check(rc == 0, "exit code %d: %s" % (rc, err.strip())):
        return
    got = [x[0] for x in lines_with(out, "order")]
    want = topo_order(names, deps, users)
    check(got == want, "build order differs from the Python Kahn order")
    level = {}
    for n in want:
        level[n] = 1 + max((level[d] for d in deps[n]), default=-1)
    got_level = {x[0]: int(x[1]) for x in lines_with(out, "level")}
    check(got_level == level, "levels differ")


def check_critical(binary, path, names, cost, deps, users):
    print("  critical: work, span, path")
    rc, out, err = run_kiln(binary, "critical", path, "--raw")
    if not check(rc == 0, "exit code %d" % rc):
        return
    order = topo_order(names, deps, users)
    work = sum(cost.values())
    span = span_of(order, cost, deps)
    check(int(lines_with(out, "work")[0][0]) == work, "work differs")
    check(int(lines_with(out, "span")[0][0]) == span, "span differs")
    path_names = lines_with(out, "path")[0]
    check(sum(cost[n] for n in path_names) == span, "path cost is not the span")
    check(all(path_names[i] in deps[path_names[i + 1]] for i in range(len(path_names) - 1)),
          "path has a step that is not a dependency")


def check_simulation(binary, path, names, cost, deps, users, worker_counts):
    print("  simulate: schedules are valid and respect scheduling bounds")
    order = topo_order(names, deps, users)
    work = sum(cost.values())
    span = span_of(order, cost, deps)
    for policy in ("fifo", "critical"):
        for w in worker_counts:
            rc, out, err = run_kiln(binary, "simulate", path, "--workers", str(w), "--policy", policy, "--raw")
            tag = "%s/%d workers" % (policy, w)
            if not check(rc == 0, tag + ": exit code %d" % rc):
                continue
            makespan = int(lines_with(out, "makespan")[0][0])
            used = int(lines_with(out, "workers")[0][0])
            runs = {}
            per_worker = {}
            dup = False
            for name, wk, s, e in lines_with(out, "run"):
                if name in runs:
                    dup = True
                runs[name] = (int(s), int(e))
                per_worker.setdefault(int(wk), []).append((int(s), int(e)))
            complete = check(not dup and set(runs) == set(names), tag + ": every task must run exactly once")
            check(all(e - s == cost[n] for n, (s, e) in runs.items() if n in cost), tag + ": run length differs from cost")
            if complete:   # the dependency check needs every task to be present
                check(all(runs[n][0] >= runs[d][1] for n in names for d in deps[n]), tag + ": task started before a dependency ended")
            overlap = any(b[0] < a[1] for spans in per_worker.values() for a, b in zip(sorted(spans), sorted(spans)[1:]))
            check(not overlap, tag + ": a worker ran two tasks at once")
            check(makespan == max((e for _, e in runs.values()), default=0), tag + ": makespan is not the last end time")
            check(makespan >= span and makespan * used >= work, tag + ": faster than the theoretical limit")
            # Graham (1966): any schedule that never leaves a worker idle while work is ready
            graham = work / used + (1 - 1 / used) * span
            check(makespan <= graham + 1e-9, tag + ": above Graham's bound %.1f" % graham)


def check_impact(binary, path, names, cost, deps, users, rng):
    print("  impact: rebuild sets for random changes")
    order = topo_order(names, deps, users)
    for _ in range(8):
        changed = rng.sample(names, min(len(names), rng.randint(1, 3)))
        rc, out, err = run_kiln(binary, "impact", path, "--changed", ",".join(changed), "--raw")
        if not check(rc == 0, "exit code %d for %s" % (rc, changed)):
            continue
        want = affected(users, changed)
        got = [x[0] for x in lines_with(out, "rebuild")]
        check(set(got) == want and len(got) == len(want), "affected set differs for %s" % changed)
        check(int(lines_with(out, "cost")[0][0]) == sum(cost[n] for n in want), "cost differs")
        check(int(lines_with(out, "span")[0][0]) == span_of(order, cost, deps, want), "partial span differs")
    rc, out, err = run_kiln(binary, "impact", path, "--changed", "no_such_target_xyz")
    check(rc == 1, "unknown target should exit with 1")


def check_hotspots(binary, path, names, cost, deps, users, top):
    print("  hotspots: top %d against brute force" % top)
    pos = {n: i for i, n in enumerate(names)}
    rows = []
    for n in names:
        a = affected(users, [n])
        rows.append((-sum(cost[x] for x in a), -len(a), pos[n], n))
    rows.sort()
    want = [(n, -na, -c) for c, na, _, n in rows[:top]]
    rc, out, err = run_kiln(binary, "hotspots", path, "--top", str(top), "--raw")
    if not check(rc == 0, "exit code %d" % rc):
        return
    got = [(x[0], int(x[1]), int(x[2])) for x in lines_with(out, "hotspot")]
    check(got == want, "top list differs")


def check_cycle(binary, path, names, deps):
    print("  cycle: reported cycle is a real one")
    rc, out, err = run_kiln(binary, "plan", path)
    check(rc == 1, "expected exit code 1, got %d" % rc)
    cyc = [l.strip() for l in err.splitlines() if " -> " in l]
    if not check(len(cyc) == 1, "expected one line with the cycle in the error output"):
        return
    chain = cyc[0].split(" -> ")
    check(len(chain) >= 2 and chain[0] == chain[-1], "cycle does not end where it started")
    check(all(chain[i + 1] in deps[chain[i]] for i in range(len(chain) - 1)), "a step in the cycle is not a dependency")


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("file")
    ap.add_argument("--bin", default=os.path.join(os.path.dirname(__file__), "..", "build", "kiln"))
    ap.add_argument("--seed", type=int, default=1)
    ap.add_argument("--expect-cycle", action="store_true", help="the file is supposed to contain a cycle")
    args = ap.parse_args()

    names, cost, deps, users = read_kiln(args.file)
    print("%s: %d targets, %d dependencies" % (args.file, len(names), sum(len(d) for d in deps.values())))
    if args.expect_cycle:
        check_cycle(args.bin, args.file, names, deps)
    else:
        rng = random.Random(args.seed)
        check_plan(args.bin, args.file, names, cost, deps, users)
        check_critical(args.bin, args.file, names, cost, deps, users)
        check_simulation(args.bin, args.file, names, cost, deps, users, [1, 2, 3, 8, 64])
        check_impact(args.bin, args.file, names, cost, deps, users, rng)
        check_hotspots(args.bin, args.file, names, cost, deps, users, min(10, len(names)))
    print("RESULT: " + ("ok" if failures == 0 else "%d check(s) failed" % failures))
    sys.exit(1 if failures else 0)


if __name__ == "__main__":
    main()
