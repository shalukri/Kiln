#include "kiln/algorithms/critical_path.hpp"

#include <algorithm>

namespace kiln {

CpmResult analyze(const Graph& g, const std::vector<int>& order) {
    const int n = g.size();
    CpmResult r;
    r.earliestStart.assign(n, 0);
    r.earliestFinish.assign(n, 0);
    r.latestStart.assign(n, 0);
    r.latestFinish.assign(n, 0);
    r.slack.assign(n, 0);
    r.bottomLevel.assign(n, 0);

    // forward pass: a task can start once its slowest dependency is done
    for (int u : order) {
        long long es = 0;
        for (int d : g.at(u).deps) es = std::max(es, r.earliestFinish[d]);
        r.earliestStart[u] = es;
        r.earliestFinish[u] = es + g.cost(u);
        r.span = std::max(r.span, r.earliestFinish[u]);
        r.work += g.cost(u);
    }

    // backward pass: how late can a task start without pushing the whole build later
    for (auto it = order.rbegin(); it != order.rend(); ++it) {
        int u = *it;
        long long lf = r.span;
        long long tail = 0;
        for (int v : g.at(u).users) {
            lf = std::min(lf, r.latestStart[v]);
            tail = std::max(tail, r.bottomLevel[v]);
        }
        r.latestFinish[u] = lf;
        r.latestStart[u] = lf - g.cost(u);
        r.slack[u] = r.latestStart[u] - r.earliestStart[u];
        r.bottomLevel[u] = g.cost(u) + tail;
    }

    // Pick the lowest id that finishes last (so the answer does not depend on hash order),
    // then walk backwards through the dependency that decided each start time.
    int last = -1;
    for (int u = 0; u < n; u++) {
        if (r.earliestFinish[u] == r.span) { last = u; break; }
    }
    while (last != -1) {
        r.path.push_back(last);
        int next = -1;
        for (int d : g.at(last).deps) {
            if (r.earliestFinish[d] == r.earliestStart[last]) { next = d; break; }
        }
        last = next;
    }
    std::reverse(r.path.begin(), r.path.end());
    return r;
}

long long spanOfSubset(const Graph& g, const std::vector<int>& order, const std::vector<char>& mask) {
    std::vector<long long> finish(g.size(), 0);
    long long best = 0;
    for (int u : order) {
        if (!mask[u]) continue;
        long long start = 0;
        for (int d : g.at(u).deps)
            if (mask[d]) start = std::max(start, finish[d]);
        finish[u] = start + g.cost(u);
        best = std::max(best, finish[u]);
    }
    return best;
}

}  // namespace kiln
