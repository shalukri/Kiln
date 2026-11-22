#pragma once
#include <vector>
#include "kiln/models/graph.hpp"

namespace kiln {

// Critical path method (the same idea project managers use for Gantt charts).
// Times are in ms and assume unlimited workers.
struct CpmResult {
    std::vector<long long> earliestStart, earliestFinish;
    std::vector<long long> latestStart, latestFinish;
    std::vector<long long> slack;         // latestStart - earliestStart, 0 means "on the critical path"
    std::vector<long long> bottomLevel;   // longest chain that starts at this task (its own cost included)
    long long work = 0;                   // sum of all costs, i.e. build time with one worker
    long long span = 0;                   // length of the longest chain, i.e. best possible build time
    std::vector<int> path;                // one longest chain, first task to last
};

// `order` has to be a complete topological order (see topoSort).
CpmResult analyze(const Graph& g, const std::vector<int>& order);

// Longest chain if only the tasks with mask[i] != 0 exist. Used to estimate partial rebuilds.
long long spanOfSubset(const Graph& g, const std::vector<int>& order, const std::vector<char>& mask);

}  // namespace kiln
