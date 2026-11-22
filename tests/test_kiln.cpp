// Small test program, no framework. Each check prints a line only when it fails.
#include <algorithm>
#include <cstdio>
#include <functional>
#include <random>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

#include "kiln/algorithms/critical_path.hpp"
#include "kiln/algorithms/reach.hpp"
#include "kiln/algorithms/toposort.hpp"
#include "kiln/services/parser.hpp"
#include "kiln/services/scheduler.hpp"

using namespace kiln;

static int failures = 0;
static int checks = 0;
#define CHECK(cond)                                                              \
    do {                                                                         \
        checks++;                                                                \
        if (!(cond)) {                                                           \
            failures++;                                                          \
            std::printf("  FAILED line %d: %s\n", __LINE__, #cond);              \
        }                                                                        \
    } while (0)

static void run(const char* name, const std::function<void()>& fn) {
    int before = failures;
    fn();
    std::printf("[%s] %s\n", failures == before ? " ok " : "FAIL", name);
}

static Graph load(const std::string& text) {
    ParseResult r = parseKilnText(text);
    if (!r.ok()) {
        std::printf("  test input did not parse: %s\n", r.errors[0].c_str());
        failures++;
    }
    return std::move(r.graph);
}

static std::vector<std::string> names(const Graph& g, const std::vector<int>& ids) {
    std::vector<std::string> out;
    for (int id : ids) out.push_back(g.name(id));
    return out;
}

// ---------------------------------------------------------------- parser

static void testParserBasics() {
    Graph g = load("a 5 : b c\nb 3\nc 2 : b\n");
    CHECK(g.size() == 3);
    CHECK(g.edgeCount() == 3);
    CHECK(g.cost(g.find("a")) == 5);
    CHECK(g.at(g.find("a")).deps.size() == 2);   // b is defined after a, forward reference works
    CHECK(g.at(g.find("b")).users.size() == 2);
    CHECK(g.find("zzz") == -1);
}

static void testParserMessyInput() {
    // comments, blank lines, tabs, CRLF, a BOM and a repeated dependency
    std::string text = "\xEF\xBB\xBF# header\r\n\r\n  a\t5  :  b   b  # trailing\r\nb 0\r\n   \r\n";
    ParseResult r = parseKilnText(text);
    CHECK(r.ok());
    CHECK(r.graph.size() == 2);
    CHECK(r.graph.edgeCount() == 1);   // "b b" counted once
    CHECK(r.graph.cost(r.graph.find("b")) == 0);
    CHECK(parseKilnText("").graph.size() == 0);
    CHECK(parseKilnText("# only a comment").ok());
}

static void testParserErrors() {
    ParseResult r = parseKilnText(
        "a 1 : ghost\n"      // 1 undefined dependency
        "b 1\n"
        "b 2\n"              // 3 duplicate
        "c -4\n"             // 4 negative cost
        "d x1\n"             // 5 not a number
        "e 1000000001\n"     // 6 too big
        "f\n"                // 7 no cost
        "g 1 : h : i\n"      // 8 two colons
        "bad$name 1\n"       // 9 bad name
        "j 1 : k$\n");       // 10 bad dependency name
    CHECK(r.errors.size() == 9);
    auto has = [&](const std::string& prefix, const std::string& part) {
        for (const std::string& e : r.errors)
            if (e.rfind(prefix, 0) == 0 && e.find(part) != std::string::npos) return true;
        return false;
    };
    CHECK(has("line 1:", "ghost"));
    CHECK(has("line 3:", "already defined on line 2"));
    CHECK(has("line 4:", "cost"));
    CHECK(has("line 5:", "cost"));
    CHECK(has("line 6:", "cost"));
    CHECK(has("line 7:", "expected"));
    CHECK(has("line 8:", "more than one"));
    CHECK(has("line 9:", "valid target name"));
    CHECK(has("line 10:", "valid target name"));
    int prevLine = 0;   // errors must come out in line order
    for (const std::string& e : r.errors) {
        int line = 0;
        std::sscanf(e.c_str(), "line %d:", &line);
        CHECK(line >= prevLine);
        prevLine = line;
    }

    // a broken definition should not make everyone who uses it fail too
    ParseResult r2 = parseKilnText("x abc\ny 1 : x\n");
    CHECK(r2.errors.size() == 1);

    CHECK(!parseKilnFile("/definitely/not/here.kiln").ok());
}

// ---------------------------------------------------------------- toposort

static void testTopoOrder() {
    Graph g = load("app 1 : lib util\nlib 1 : base\nutil 1 : base\nbase 1\nextra 1\n");
    TopoResult t = topoSort(g);
    CHECK(t.stuck == 0);
    CHECK(t.order.size() == 5);
    std::vector<int> pos(5);
    for (int i = 0; i < 5; i++) pos[t.order[i]] = i;
    for (int u = 0; u < g.size(); u++)
        for (int d : g.at(u).deps) CHECK(pos[d] < pos[u]);
    // only base and extra are ready at the start and base comes first in the file. Once base
    // is done, lib and util (lower ids than extra) jump ahead of extra, then app is released
    CHECK(names(g, t.order) == (std::vector<std::string>{"base", "lib", "util", "app", "extra"}));
    CHECK(t.level[g.find("base")] == 0 && t.level[g.find("lib")] == 1 && t.level[g.find("app")] == 2);
}

static void testCycles() {
    CHECK(findCycle(load("a 1\nb 1 : a\n")).empty());
    CHECK(findCycle(Graph()).empty());

    Graph self = load("a 1 : a\n");
    std::vector<int> c = findCycle(self);
    CHECK(c.size() == 2 && c[0] == c[1]);
    CHECK(topoSort(self).stuck == 1);

    Graph two = load("a 1 : b\nb 1 : a\n");
    CHECK(findCycle(two).size() == 3);

    // cycle hiding behind healthy tasks, plus an innocent bystander that waits on it
    Graph g = load("ok 1\nuser 1 : ok p\np 1 : q\nq 1 : r\nr 1 : s ok\ns 1 : p\nwaiter 1 : s\n");
    std::vector<int> cyc = findCycle(g);
    CHECK(cyc.size() >= 3 && cyc.front() == cyc.back());
    for (std::size_t i = 0; i + 1 < cyc.size(); i++) {   // every step must be a real dependency
        const auto& deps = g.at(cyc[i]).deps;
        CHECK(std::find(deps.begin(), deps.end(), cyc[i + 1]) != deps.end());
    }
    TopoResult t = topoSort(g);
    CHECK(t.stuck == 6);   // user, p, q, r, s, waiter. only "ok" gets built
    CHECK(t.level[g.find("p")] == -1);
}

static void testDeepChain() {
    // 200k tasks in a row. A recursive DFS would overflow the stack here.
    const int n = 200000;
    Graph g;
    for (int i = 0; i < n; i++) g.addTask("t" + std::to_string(i), 1);
    for (int i = 1; i < n; i++) g.addEdge(i, i - 1);
    TopoResult t = topoSort(g);
    CHECK(t.stuck == 0 && t.level[n - 1] == n - 1);
    CHECK(findCycle(g).empty());
    CpmResult c = analyze(g, t.order);
    CHECK(c.span == n && c.path.size() == static_cast<std::size_t>(n));

    g.addEdge(0, n - 1);   // close the loop
    CHECK(findCycle(g).size() == static_cast<std::size_t>(n) + 1);
}

// ---------------------------------------------------------------- critical path

static void testCriticalPath() {
    // diamond: a(2) -> b(3), c(1) -> d(4)
    Graph g = load("a 2\nb 3 : a\nc 1 : a\nd 4 : b c\n");
    TopoResult t = topoSort(g);
    CpmResult r = analyze(g, t.order);
    CHECK(r.work == 10);
    CHECK(r.span == 9);
    CHECK(names(g, r.path) == (std::vector<std::string>{"a", "b", "d"}));
    CHECK(r.slack[g.find("a")] == 0 && r.slack[g.find("b")] == 0 && r.slack[g.find("d")] == 0);
    CHECK(r.slack[g.find("c")] == 2);
    CHECK(r.earliestStart[g.find("d")] == 5);
    CHECK(r.bottomLevel[g.find("a")] == 9 && r.bottomLevel[g.find("c")] == 5);
}

static void testCriticalPathEdgeCases() {
    Graph empty;
    CpmResult e = analyze(empty, {});
    CHECK(e.span == 0 && e.work == 0 && e.path.empty());

    Graph one = load("solo 7\n");
    CpmResult o = analyze(one, topoSort(one).order);
    CHECK(o.span == 7 && o.path.size() == 1);

    // everything free: span 0, still returns a sensible single task path
    Graph zero = load("a 0\nb 0 : a\n");
    CpmResult z = analyze(zero, topoSort(zero).order);
    CHECK(z.span == 0 && z.slack[0] == 0);

    // two equally long chains, both have zero slack, the answer is the one with the lower id
    Graph tie = load("a 5\nb 5\nend 1 : a b\n");
    CpmResult t = analyze(tie, topoSort(tie).order);
    CHECK(t.span == 6 && t.slack[0] == 0 && t.slack[1] == 0);
    CHECK(names(tie, t.path) == (std::vector<std::string>{"a", "end"}));
}

// ---------------------------------------------------------------- scheduler

static void checkScheduleIsValid(const Graph& g, const Schedule& s) {
    const int n = g.size();
    CHECK(static_cast<int>(s.runs.size()) == n);
    std::vector<long long> start(n, -1), end(n, -1);
    std::vector<std::vector<std::pair<long long, long long>>> perWorker(s.workers);
    long long last = 0;
    for (const Run& r : s.runs) {
        CHECK(start[r.task] == -1);                       // every task runs once
        start[r.task] = r.start;
        end[r.task] = r.end;
        CHECK(r.end - r.start == g.cost(r.task));
        perWorker[r.worker].push_back({r.start, r.end});
        last = std::max(last, r.end);
    }
    CHECK(last == s.makespan);
    for (int u = 0; u < n; u++)
        for (int d : g.at(u).deps) CHECK(start[u] >= end[d]);
    for (auto& w : perWorker) {
        std::sort(w.begin(), w.end());
        for (std::size_t i = 1; i < w.size(); i++) CHECK(w[i].first >= w[i - 1].second);
    }
}

static void testSchedulerPolicies() {
    // six tiny independent jobs listed first, then one long chain b -> c.
    // fifo starts b too late, critical-first starts it right away.
    std::string text;
    for (int i = 1; i <= 6; i++) text += "a" + std::to_string(i) + " 1\n";
    text += "b 10\nc 10 : b\n";
    Graph g = load(text);
    CpmResult c = analyze(g, topoSort(g).order);
    CHECK(c.span == 20);

    Schedule fifo = simulate(g, c.bottomLevel, 2, Policy::Fifo);
    Schedule crit = simulate(g, c.bottomLevel, 2, Policy::CriticalFirst);
    CHECK(fifo.makespan == 23);
    CHECK(crit.makespan == 20);
    checkScheduleIsValid(g, fifo);
    checkScheduleIsValid(g, crit);

    CHECK(simulate(g, c.bottomLevel, 1, Policy::Fifo).makespan == c.work);
    CHECK(simulate(g, c.bottomLevel, 100, Policy::CriticalFirst).makespan == c.span);
    CHECK(simulate(g, c.bottomLevel, 1000000000, Policy::Fifo).workers == g.size());   // clamped
}

static void testSchedulerEdgeCases() {
    Graph empty;
    Schedule s = simulate(empty, {}, 3, Policy::Fifo);
    CHECK(s.makespan == 0 && s.runs.empty());

    Graph zero = load("a 0\nb 0 : a\nc 0 : b\n");   // chain of free tasks must not loop forever
    Schedule z = simulate(zero, {}, 2, Policy::Fifo);
    CHECK(z.makespan == 0 && z.runs.size() == 3);

    bool threw = false;
    try { simulate(zero, {}, 0, Policy::Fifo); } catch (const std::invalid_argument&) { threw = true; }
    CHECK(threw);

    Graph cyc = load("a 1 : b\nb 1 : a\n");
    threw = false;
    try { simulate(cyc, {}, 2, Policy::Fifo); } catch (const std::runtime_error&) { threw = true; }
    CHECK(threw);

    threw = false;
    try { simulate(zero, {}, 2, Policy::CriticalFirst); } catch (const std::invalid_argument&) { threw = true; }
    CHECK(threw);   // bottom levels are required for this policy
}

// builds a random acyclic graph: task i may only depend on lower numbered tasks
static Graph randomDag(std::mt19937& rng, int n, int maxDeps, int maxCost) {
    Graph g;
    for (int i = 0; i < n; i++) g.addTask("t" + std::to_string(i), static_cast<long long>(rng() % (maxCost + 1)));
    for (int i = 1; i < n; i++) {
        int k = static_cast<int>(rng() % (maxDeps + 1));
        std::set<int> chosen;
        for (int j = 0; j < k; j++) chosen.insert(static_cast<int>(rng() % i));
        for (int d : chosen) g.addEdge(i, d);
    }
    return g;
}

static void testRandomGraphs() {
    std::mt19937 rng(12345);
    for (int round = 0; round < 150; round++) {
        Graph g = randomDag(rng, 1 + static_cast<int>(rng() % 60), 4, 50);
        TopoResult t = topoSort(g);
        CHECK(t.stuck == 0);
        CpmResult c = analyze(g, t.order);

        // brute force longest path with memoised recursion (graphs here are small)
        std::vector<long long> memo(g.size(), -1);
        std::function<long long(int)> finish = [&](int u) -> long long {
            if (memo[u] >= 0) return memo[u];
            long long best = 0;
            for (int d : g.at(u).deps) best = std::max(best, finish(d));
            return memo[u] = best + g.cost(u);
        };
        long long span = 0;
        for (int u = 0; u < g.size(); u++) span = std::max(span, finish(u));
        CHECK(c.span == span);

        long long pathCost = 0;
        for (int id : c.path) pathCost += g.cost(id);
        CHECK(pathCost == c.span);
        for (int u = 0; u < g.size(); u++) CHECK(c.slack[u] >= 0);

        for (int w : {1, 2, 3, 8}) {
            for (Policy p : {Policy::Fifo, Policy::CriticalFirst}) {
                Schedule s = simulate(g, c.bottomLevel, w, p);
                checkScheduleIsValid(g, s);
                double eff = s.workers;
                // Graham's bound for any greedy schedule: T <= work/p + (1 - 1/p) * span
                CHECK(static_cast<double>(s.makespan) <= static_cast<double>(c.work) / eff + (1.0 - 1.0 / eff) * static_cast<double>(c.span) + 1e-9);
                CHECK(s.makespan >= c.span);
                CHECK(s.makespan * s.workers >= c.work);
            }
        }
    }
}

// ---------------------------------------------------------------- impact and hotspots

static void testImpact() {
    Graph g = load("a 1\nb 2 : a\nc 3 : a\nd 4 : b c\ne 5 : d\nf 6\n");
    std::vector<char> m = affectedBy(g, {g.find("b")});
    CHECK(m[g.find("b")] && m[g.find("d")] && m[g.find("e")]);
    CHECK(!m[g.find("a")] && !m[g.find("c")] && !m[g.find("f")]);

    std::vector<char> both = affectedBy(g, {g.find("b"), g.find("c"), g.find("b")});   // repeats are fine
    int count = 0;
    for (char x : both) count += x;
    CHECK(count == 4);

    TopoResult t = topoSort(g);
    CHECK(spanOfSubset(g, t.order, m) == 2 + 4 + 5);   // b -> d -> e
    CHECK(spanOfSubset(g, t.order, affectedBy(g, {g.find("a")})) == 1 + 3 + 4 + 5);
    CHECK(affectedBy(g, {}).size() == 6);
}

static void testHotspots() {
    Graph g = load("a 1\nb 2 : a\nc 3 : a\nd 4 : b c\ne 5 : d\nf 6\n");
    std::vector<Hotspot> h = topHotspots(g, 3);
    CHECK(h.size() == 3);
    CHECK(g.name(h[0].task) == "a" && h[0].affected == 5 && h[0].cost == 1 + 2 + 3 + 4 + 5);
    CHECK(g.name(h[1].task) == "b" || g.name(h[1].task) == "c");
    CHECK(h[0].cost >= h[1].cost && h[1].cost >= h[2].cost);
    CHECK(topHotspots(g, 0).empty());
    CHECK(topHotspots(g, 100).size() == 6);
    CHECK(topHotspots(Graph(), 5).empty());

    // ties: same cost and same count, lower id wins
    Graph t = load("x 5\ny 5\n");
    std::vector<Hotspot> ht = topHotspots(t, 2);
    CHECK(t.name(ht[0].task) == "x" && t.name(ht[1].task) == "y");

    // compare with a plain "run affectedBy for every task and sort" version
    std::mt19937 rng(777);
    for (int round = 0; round < 40; round++) {
        Graph r = randomDag(rng, 2 + static_cast<int>(rng() % 50), 3, 30);
        std::vector<Hotspot> all;
        for (int u = 0; u < r.size(); u++) {
            std::vector<char> m = affectedBy(r, {u});
            Hotspot x{u, 0, 0};
            for (int v = 0; v < r.size(); v++)
                if (m[v]) { x.affected++; x.cost += r.cost(v); }
            all.push_back(x);
        }
        std::sort(all.begin(), all.end(), [](const Hotspot& a, const Hotspot& b) {
            if (a.cost != b.cost) return a.cost > b.cost;
            if (a.affected != b.affected) return a.affected > b.affected;
            return a.task < b.task;
        });
        std::size_t k = 1 + rng() % 8;
        std::vector<Hotspot> got = topHotspots(r, k);
        CHECK(got.size() == std::min<std::size_t>(k, all.size()));
        for (std::size_t i = 0; i < got.size(); i++)
            CHECK(got[i].task == all[i].task && got[i].cost == all[i].cost && got[i].affected == all[i].affected);
    }
}

int main() {
    run("parser: basic file, forward references", testParserBasics);
    run("parser: comments, CRLF, BOM, tabs, repeated deps", testParserMessyInput);
    run("parser: every kind of mistake is reported", testParserErrors);
    run("toposort: valid order, tie break, levels", testTopoOrder);
    run("cycles: self loop, 2-cycle, hidden cycle, blocked tasks", testCycles);
    run("deep chain of 200000 tasks (no stack overflow)", testDeepChain);
    run("critical path: diamond example with slack", testCriticalPath);
    run("critical path: empty, single, zero cost, ties", testCriticalPathEdgeCases);
    run("scheduler: fifo vs critical-first, bounds", testSchedulerPolicies);
    run("scheduler: empty, free tasks, bad input, cycle", testSchedulerEdgeCases);
    run("random graphs: topo, span vs brute force, schedules", testRandomGraphs);
    run("impact: reverse BFS and partial rebuild span", testImpact);
    run("hotspots: top-k heap vs full sort", testHotspots);
    std::printf("\n%d checks, %d failed\n", checks, failures);
    return failures ? 1 : 0;
}
