#include "kiln/algorithms/toposort.hpp"

#include <algorithm>
#include <functional>
#include <queue>

namespace kiln {

TopoResult topoSort(const Graph& g) {
    const int n = g.size();
    TopoResult r;
    r.order.reserve(n);
    r.level.assign(n, 0);

    std::vector<int> waiting(n);
    // min-heap on id, this is what makes the tie-breaking follow file order
    std::priority_queue<int, std::vector<int>, std::greater<int>> ready;
    for (int i = 0; i < n; i++) {
        waiting[i] = static_cast<int>(g.at(i).deps.size());
        if (waiting[i] == 0) ready.push(i);
    }

    std::vector<char> done(n, 0);
    while (!ready.empty()) {
        int u = ready.top();
        ready.pop();
        done[u] = 1;
        r.order.push_back(u);
        for (int v : g.at(u).users) {
            r.level[v] = std::max(r.level[v], r.level[u] + 1);
            if (--waiting[v] == 0) ready.push(v);
        }
    }

    r.stuck = n - static_cast<int>(r.order.size());
    for (int i = 0; i < n; i++)
        if (!done[i]) r.level[i] = -1;
    return r;
}

std::vector<int> findCycle(const Graph& g) {
    const int n = g.size();
    enum : char { WHITE = 0, GRAY = 1, BLACK = 2 };
    std::vector<char> color(n, WHITE);

    struct Frame {
        int node;
        std::size_t next;   // index of the next dep to look at
    };
    std::vector<Frame> stack;

    for (int root = 0; root < n; root++) {
        if (color[root] != WHITE) continue;
        color[root] = GRAY;
        stack.push_back({root, 0});

        while (!stack.empty()) {
            Frame& f = stack.back();
            const std::vector<int>& deps = g.at(f.node).deps;
            if (f.next == deps.size()) {
                color[f.node] = BLACK;
                stack.pop_back();
                continue;
            }
            int w = deps[f.next++];
            if (color[w] == GRAY) {
                // w is already on the current path, so the path from w to here is a cycle
                std::size_t i = 0;
                while (stack[i].node != w) i++;
                std::vector<int> cycle;
                for (; i < stack.size(); i++) cycle.push_back(stack[i].node);
                cycle.push_back(w);
                return cycle;
            }
            if (color[w] == WHITE) {
                color[w] = GRAY;
                stack.push_back({w, 0});   // `f` is not used after this line
            }
        }
    }
    return {};
}

}  // namespace kiln
