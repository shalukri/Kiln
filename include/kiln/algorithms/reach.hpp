#pragma once
#include <cstddef>
#include <vector>
#include "kiln/models/graph.hpp"

namespace kiln {

// mask[i] == 1 for every task that has to be rebuilt if the `changed` tasks change.
// The changed tasks are part of the answer. BFS over the reverse edges.
std::vector<char> affectedBy(const Graph& g, const std::vector<int>& changed);

struct Hotspot {
    int task;
    int affected;        // how many tasks get rebuilt (including itself)
    long long cost;      // total build time of those tasks
};

// The k tasks whose change is the most expensive, best first.
// Order: higher cost, then more affected tasks, then lower id.
std::vector<Hotspot> topHotspots(const Graph& g, std::size_t k);

}  // namespace kiln
