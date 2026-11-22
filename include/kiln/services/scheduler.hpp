#pragma once
#include <vector>
#include "kiln/models/graph.hpp"

namespace kiln {

enum class Policy {
    Fifo,           // first task that became ready runs first
    CriticalFirst   // task with the longest chain still ahead of it runs first
};

struct Run {
    int task;
    int worker;
    long long start;
    long long end;
};

struct Schedule {
    long long makespan = 0;
    int workers = 0;               // workers actually used (never more than there are tasks)
    double utilization = 0;        // busy time / (workers * makespan)
    std::vector<Run> runs;         // in the order the tasks were started
};

// Event driven simulation of `workers` identical workers building the graph.
// A worker never sits idle while something is ready, so this is a "greedy list schedule".
// `bottomLevel` comes from analyze() and is only read by the CriticalFirst policy.
// Throws std::invalid_argument for workers < 1 and std::runtime_error if the graph has a cycle.
Schedule simulate(const Graph& g, const std::vector<long long>& bottomLevel, int workers, Policy policy);

}  // namespace kiln
