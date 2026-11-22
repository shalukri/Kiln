// kiln: plan and analyse a build before running it.
//
//   kiln plan      <file> [--all] [--raw]
//   kiln critical  <file> [--raw]
//   kiln simulate  <file> --workers N [--policy fifo|critical] [--trace] [--all] [--raw]
//   kiln sweep     <file>
//   kiln impact    <file> --changed a,b,c [--all] [--raw]
//   kiln hotspots  <file> [--top K] [--raw]
//
// Exit codes: 0 fine, 1 bad input file (syntax error or cycle), 2 bad command line.
// The --raw flag prints plain "key value" lines, which tools/verify.py reads.
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "kiln/algorithms/critical_path.hpp"
#include "kiln/algorithms/reach.hpp"
#include "kiln/algorithms/toposort.hpp"
#include "kiln/models/graph.hpp"
#include "kiln/services/parser.hpp"
#include "kiln/services/scheduler.hpp"

using namespace kiln;

namespace {

struct Args {
    std::string command;
    std::string file;
    std::map<std::string, std::string> values;   // --workers 4
    std::set<std::string> flags;                 // --raw
    bool has(const std::string& f) const { return flags.count(f) > 0; }
    bool has_value(const std::string& k) const { return values.count(k) > 0; }
};

int usage() {
    std::fprintf(stderr,
        "usage: kiln <command> <file> [options]\n"
        "  plan      build order and parallel levels\n"
        "  critical  critical path and slack\n"
        "  simulate  --workers N [--policy fifo|critical] [--trace]\n"
        "  sweep     how build time changes with more workers\n"
        "  impact    --changed a,b,c   what has to be rebuilt\n"
        "  hotspots  [--top K]         most expensive targets to touch\n"
        "other flags: --all (do not shorten long lists), --raw (machine readable)\n");
    return 2;
}

bool parseArgs(int argc, char** argv, Args& a) {
    static const std::set<std::string> flagNames = {"raw", "trace", "all"};
    static const std::set<std::string> valueNames = {"workers", "policy", "changed", "top"};
    if (argc < 3) return false;
    a.command = argv[1];
    a.file = argv[2];
    for (int i = 3; i < argc; i++) {
        std::string s = argv[i];
        if (s.rfind("--", 0) != 0) {
            std::fprintf(stderr, "unexpected argument '%s'\n", s.c_str());
            return false;
        }
        s = s.substr(2);
        if (flagNames.count(s)) {
            a.flags.insert(s);
        } else if (valueNames.count(s)) {
            if (i + 1 >= argc) {
                std::fprintf(stderr, "--%s needs a value\n", s.c_str());
                return false;
            }
            a.values[s] = argv[++i];
        } else {
            std::fprintf(stderr, "unknown option --%s\n", s.c_str());
            return false;
        }
    }
    return true;
}

bool parseNumber(const std::string& s, long long lo, long long hi, long long& out) {
    if (s.empty()) return false;
    char* end = nullptr;
    long long v = std::strtoll(s.c_str(), &end, 10);
    if (*end != '\0' || v < lo || v > hi) return false;
    out = v;
    return true;
}

// Reads the file and makes sure it is a valid, acyclic graph. Prints the problem if not.
bool loadGraph(const std::string& path, Graph& g, TopoResult& topo) {
    ParseResult parsed = parseKilnFile(path);
    if (!parsed.ok()) {
        std::fprintf(stderr, "error: %s has %zu problem(s)\n", path.c_str(), parsed.errors.size());
        for (std::size_t i = 0; i < parsed.errors.size() && i < 10; i++)
            std::fprintf(stderr, "  %s\n", parsed.errors[i].c_str());
        if (parsed.errors.size() > 10) std::fprintf(stderr, "  ... and %zu more\n", parsed.errors.size() - 10);
        return false;
    }
    g = std::move(parsed.graph);
    topo = topoSort(g);
    if (topo.stuck > 0) {
        std::vector<int> cycle = findCycle(g);
        std::fprintf(stderr, "error: dependency cycle\n  ");
        for (std::size_t i = 0; i < cycle.size(); i++)
            std::fprintf(stderr, "%s%s", i ? " -> " : "", g.name(cycle[i]).c_str());
        std::fprintf(stderr, "\n%d target(s) can never be built because of cycles\n", topo.stuck);
        return false;
    }
    return true;
}

// ---------------------------------------------------------------- plan

int cmdPlan(const Graph& g, const TopoResult& topo, const Args& a) {
    const int n = g.size();
    if (a.has("raw")) {
        for (int id : topo.order) std::printf("order %s\n", g.name(id).c_str());
        for (int id : topo.order) std::printf("level %s %d\n", g.name(id).c_str(), topo.level[id]);
        return 0;
    }
    std::printf("%d targets, %zu dependencies, total work %lld ms\n", n, g.edgeCount(), g.totalWork());

    const int limit = a.has("all") ? n : 40;
    std::printf("\nbuild order:\n");
    for (int i = 0; i < n && i < limit; i++) {
        int id = topo.order[i];
        std::printf("  %3d. %s (%lld ms)\n", i + 1, g.name(id).c_str(), g.cost(id));
    }
    if (n > limit) std::printf("  ... %d more (use --all)\n", n - limit);

    int levels = 0;
    for (int id = 0; id < n; id++) levels = std::max(levels, topo.level[id] + 1);
    std::vector<std::vector<int>> byLevel(levels);
    for (int id : topo.order) byLevel[topo.level[id]].push_back(id);

    std::printf("\nparallel levels (targets in one level do not depend on each other):\n");
    const int levelLimit = a.has("all") ? levels : 15;
    for (int l = 0; l < levels && l < levelLimit; l++) {
        std::printf("  level %d: ", l);
        for (std::size_t i = 0; i < byLevel[l].size(); i++) {
            if (i == 8 && !a.has("all")) {
                std::printf(", ... (+%zu more)", byLevel[l].size() - 8);
                break;
            }
            std::printf("%s%s", i ? ", " : "", g.name(byLevel[l][i]).c_str());
        }
        std::printf("\n");
    }
    if (levels > levelLimit) std::printf("  ... %d more levels (use --all)\n", levels - levelLimit);
    return 0;
}

// ---------------------------------------------------------------- critical

int cmdCritical(const Graph& g, const TopoResult& topo, const Args& a) {
    CpmResult c = analyze(g, topo.order);
    int zero = 0;
    for (int id = 0; id < g.size(); id++) zero += c.slack[id] == 0;

    if (a.has("raw")) {
        std::printf("work %lld\nspan %lld\nzero_slack %d\npath", c.work, c.span, zero);
        for (int id : c.path) std::printf(" %s", g.name(id).c_str());
        std::printf("\n");
        return 0;
    }
    std::printf("total work     : %lld ms (one worker does everything)\n", c.work);
    std::printf("critical path  : %lld ms (best possible build time, even with endless workers)\n", c.span);
    if (c.span > 0) std::printf("best speedup   : %.2fx\n", static_cast<double>(c.work) / static_cast<double>(c.span));
    std::printf("%d of %d targets have zero slack\n\n", zero, g.size());

    std::printf("one critical path (%zu targets):\n  ", c.path.size());
    for (std::size_t i = 0; i < c.path.size(); i++) {
        if (i) std::printf(i % 4 == 0 ? " ->\n  " : " -> ");   // wrap every 4 names
        std::printf("%s", g.name(c.path[i]).c_str());
    }
    std::printf("\n");

    std::vector<int> loose;
    for (int id = 0; id < g.size(); id++)
        if (c.slack[id] > 0) loose.push_back(id);
    std::sort(loose.begin(), loose.end(), [&](int x, int y) {
        return c.slack[x] != c.slack[y] ? c.slack[x] > c.slack[y] : x < y;
    });
    if (!loose.empty()) {
        std::printf("\ntargets with the most slack (they can run late for free):\n");
        for (std::size_t i = 0; i < loose.size() && i < 5; i++)
            std::printf("  %-24s slack %lld ms\n", g.name(loose[i]).c_str(), c.slack[loose[i]]);
    }
    return 0;
}

// ---------------------------------------------------------------- simulate / sweep

int cmdSimulate(const Graph& g, const TopoResult& topo, const Args& a) {
    long long workers = 4;
    if (a.has_value("workers") && !parseNumber(a.values.at("workers"), 1, 1000000, workers)) {
        std::fprintf(stderr, "--workers must be a whole number from 1 to 1000000\n");
        return 2;
    }
    CpmResult c = analyze(g, topo.order);
    const int w = static_cast<int>(workers);

    if (a.has("raw")) {
        Policy p = Policy::CriticalFirst;
        if (a.has_value("policy")) {
            const std::string& s = a.values.at("policy");
            if (s == "fifo") p = Policy::Fifo;
            else if (s != "critical") { std::fprintf(stderr, "--policy must be fifo or critical\n"); return 2; }
        }
        Schedule s = simulate(g, c.bottomLevel, w, p);
        std::printf("makespan %lld\nworkers %d\n", s.makespan, s.workers);
        for (const Run& r : s.runs) std::printf("run %s %d %lld %lld\n", g.name(r.task).c_str(), r.worker, r.start, r.end);
        return 0;
    }

    Schedule fifo = simulate(g, c.bottomLevel, w, Policy::Fifo);
    Schedule crit = simulate(g, c.bottomLevel, w, Policy::CriticalFirst);
    long long perWorker = (c.work + fifo.workers - 1) / fifo.workers;
    long long lower = std::max(c.span, perWorker);

    std::printf("%d worker(s)\n\n", fifo.workers);
    std::printf("  %-16s %12s %12s\n", "policy", "build time", "utilization");
    for (const Schedule* s : {&fifo, &crit})
        std::printf("  %-16s %9lld ms %11.1f%%\n", s == &fifo ? "fifo" : "critical-first", s->makespan, s->utilization * 100.0);
    std::printf("\nno schedule can beat %lld ms here (the larger of critical path %lld ms and work/workers %lld ms)\n",
                lower, c.span, perWorker);
    if (lower > 0)
        std::printf("critical-first is %.1f%% above that limit, fifo is %.1f%% above\n",
                    100.0 * static_cast<double>(crit.makespan - lower) / static_cast<double>(lower),
                    100.0 * static_cast<double>(fifo.makespan - lower) / static_cast<double>(lower));

    if (a.has("trace")) {
        std::printf("\ncritical-first schedule:\n");
        const std::size_t limit = a.has("all") ? crit.runs.size() : 40;
        for (std::size_t i = 0; i < crit.runs.size() && i < limit; i++) {
            const Run& r = crit.runs[i];
            std::printf("  t=%-7lld worker %-2d %-24s (%lld ms)\n", r.start, r.worker, g.name(r.task).c_str(), r.end - r.start);
        }
        if (crit.runs.size() > limit) std::printf("  ... %zu more (use --all)\n", crit.runs.size() - limit);
    }
    return 0;
}

int cmdSweep(const Graph& g, const TopoResult& topo) {
    CpmResult c = analyze(g, topo.order);
    Schedule one = simulate(g, c.bottomLevel, 1, Policy::CriticalFirst);
    std::printf("  %7s %12s %9s %12s\n", "workers", "build time", "speedup", "utilization");
    for (int w = 1; w <= 1024; w *= 2) {
        Schedule s = simulate(g, c.bottomLevel, w, Policy::CriticalFirst);
        double speedup = s.makespan > 0 ? static_cast<double>(one.makespan) / static_cast<double>(s.makespan) : 1.0;
        std::printf("  %7d %9lld ms %8.2fx %11.1f%%\n", w, s.makespan, speedup, s.utilization * 100.0);
        if (s.makespan == c.span || w >= g.size()) break;   // more workers cannot help any more
    }
    std::printf("\nlimit with endless workers: %lld ms\n", c.span);
    return 0;
}

// ---------------------------------------------------------------- impact / hotspots

int cmdImpact(const Graph& g, const TopoResult& topo, const Args& a) {
    if (!a.has_value("changed")) {
        std::fprintf(stderr, "impact needs --changed a,b,c\n");
        return 2;
    }
    std::vector<int> changed;
    std::string list = a.values.at("changed");
    std::size_t pos = 0;
    bool bad = false;
    while (pos <= list.size()) {
        std::size_t comma = list.find(',', pos);
        std::string name = list.substr(pos, comma == std::string::npos ? std::string::npos : comma - pos);
        if (!name.empty()) {
            int id = g.find(name);
            if (id < 0) {
                std::fprintf(stderr, "error: no target called '%s'\n", name.c_str());
                bad = true;
            } else {
                changed.push_back(id);
            }
        }
        if (comma == std::string::npos) break;
        pos = comma + 1;
    }
    if (bad) return 1;
    if (changed.empty()) {
        std::fprintf(stderr, "--changed needs at least one target name\n");
        return 2;
    }

    std::vector<char> mask = affectedBy(g, changed);
    long long cost = 0;
    int count = 0;
    for (int id = 0; id < g.size(); id++) {
        if (mask[id]) {
            cost += g.cost(id);
            count++;
        }
    }
    long long span = spanOfSubset(g, topo.order, mask);

    if (a.has("raw")) {
        std::printf("affected %d\ncost %lld\nspan %lld\n", count, cost, span);
        for (int id : topo.order)
            if (mask[id]) std::printf("rebuild %s\n", g.name(id).c_str());
        return 0;
    }
    long long total = g.totalWork();
    std::printf("%d of %d targets need a rebuild\n", count, g.size());
    std::printf("one worker      : %lld ms (%.1f%% of a full build)\n", cost, total ? 100.0 * static_cast<double>(cost) / static_cast<double>(total) : 0.0);
    std::printf("endless workers : %lld ms\n\n", span);
    int shown = 0;
    const int limit = a.has("all") ? g.size() : 40;
    for (int id : topo.order) {
        if (!mask[id]) continue;
        if (shown++ == limit) {
            std::printf("  ... %d more (use --all)\n", count - limit);
            break;
        }
        std::printf("  %s (%lld ms)\n", g.name(id).c_str(), g.cost(id));
    }
    return 0;
}

int cmdHotspots(const Graph& g, const Args& a) {
    long long top = 10;
    if (a.has_value("top") && !parseNumber(a.values.at("top"), 1, 1000000, top)) {
        std::fprintf(stderr, "--top must be a whole number from 1 to 1000000\n");
        return 2;
    }
    std::vector<Hotspot> hs = topHotspots(g, static_cast<std::size_t>(top));
    if (a.has("raw")) {
        for (const Hotspot& h : hs) std::printf("hotspot %s %d %lld\n", g.name(h.task).c_str(), h.affected, h.cost);
        return 0;
    }
    long long total = g.totalWork();
    std::printf("targets that cost the most when they change:\n\n");
    std::printf("  %4s  %-24s %9s %12s %8s\n", "rank", "target", "rebuilds", "rebuild time", "of total");
    int rank = 1;
    for (const Hotspot& h : hs)
        std::printf("  %4d  %-24s %9d %9lld ms %7.1f%%\n", rank++, g.name(h.task).c_str(), h.affected, h.cost,
                    total ? 100.0 * static_cast<double>(h.cost) / static_cast<double>(total) : 0.0);
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    Args a;
    if (!parseArgs(argc, argv, a)) return usage();

    static const std::set<std::string> known = {"plan", "critical", "simulate", "sweep", "impact", "hotspots"};
    if (!known.count(a.command)) {
        std::fprintf(stderr, "unknown command '%s'\n", a.command.c_str());
        return usage();
    }

    Graph g;
    TopoResult topo;
    if (!loadGraph(a.file, g, topo)) return 1;

    try {
        if (a.command == "plan") return cmdPlan(g, topo, a);
        if (a.command == "critical") return cmdCritical(g, topo, a);
        if (a.command == "simulate") return cmdSimulate(g, topo, a);
        if (a.command == "sweep") return cmdSweep(g, topo);
        if (a.command == "impact") return cmdImpact(g, topo, a);
        return cmdHotspots(g, a);
    } catch (const std::exception& e) {
        std::fprintf(stderr, "error: %s\n", e.what());
        return 1;
    }
}
