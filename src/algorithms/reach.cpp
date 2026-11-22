#include "kiln/algorithms/reach.hpp"

#include <algorithm>
#include <queue>

namespace kiln {

std::vector<char> affectedBy(const Graph& g, const std::vector<int>& changed) {
    std::vector<char> mask(g.size(), 0);
    std::vector<int> queue;
    for (int c : changed) {
        if (!mask[c]) {
            mask[c] = 1;
            queue.push_back(c);
        }
    }
    for (std::size_t head = 0; head < queue.size(); head++) {
        for (int v : g.at(queue[head]).users) {
            if (!mask[v]) {
                mask[v] = 1;
                queue.push_back(v);
            }
        }
    }
    return mask;
}

std::vector<Hotspot> topHotspots(const Graph& g, std::size_t k) {
    std::vector<Hotspot> result;
    if (k == 0) return result;

    auto better = [](const Hotspot& a, const Hotspot& b) {
        if (a.cost != b.cost) return a.cost > b.cost;
        if (a.affected != b.affected) return a.affected > b.affected;
        return a.task < b.task;
    };
    // The heap keeps the k best seen so far. With this comparator its top is the WORST of
    // them, so a new candidate only has to beat the top to get in. O(V log k) for the heap.
    std::priority_queue<Hotspot, std::vector<Hotspot>, decltype(better)> heap(better);

    const int n = g.size();
    // Instead of clearing a visited array for every start node (O(V) each time) we store the
    // id of the start node that last touched an entry, so "seen" means seen[x] == start.
    std::vector<int> seen(n, -1);
    std::vector<int> queue;
    for (int start = 0; start < n; start++) {
        queue.clear();
        queue.push_back(start);
        seen[start] = start;
        long long cost = 0;
        for (std::size_t head = 0; head < queue.size(); head++) {
            int u = queue[head];
            cost += g.cost(u);
            for (int v : g.at(u).users) {
                if (seen[v] != start) {
                    seen[v] = start;
                    queue.push_back(v);
                }
            }
        }
        Hotspot h{start, static_cast<int>(queue.size()), cost};
        if (heap.size() < k) {
            heap.push(h);
        } else if (better(h, heap.top())) {
            heap.pop();
            heap.push(h);
        }
    }

    while (!heap.empty()) {
        result.push_back(heap.top());
        heap.pop();
    }
    std::reverse(result.begin(), result.end());
    return result;
}

}  // namespace kiln
