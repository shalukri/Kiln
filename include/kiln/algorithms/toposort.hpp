#pragma once
#include <vector>
#include "kiln/models/graph.hpp"

namespace kiln {

struct TopoResult {
    std::vector<int> order;   // dependencies first
    std::vector<int> level;   // 0 for tasks with no deps, else 1 + max level of deps. -1 if stuck
    int stuck = 0;            // tasks that never became ready (they sit behind a cycle)
};

// Kahn's algorithm. When several tasks are ready at once, the one that appears first in the
// input file goes first, so the output follows the order the user wrote things in.
TopoResult topoSort(const Graph& g);

// Returns one cycle as [a, b, c, a], meaning a needs b, b needs c, c needs a.
// Empty vector if the graph has no cycle. Iterative on purpose: a chain of 200k targets
// would blow the call stack with a recursive DFS.
std::vector<int> findCycle(const Graph& g);

}  // namespace kiln
