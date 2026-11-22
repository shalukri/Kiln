#pragma once
#include <cstddef>
#include <string>
#include <unordered_map>
#include <vector>

namespace kiln {

// One thing that has to be built (a source file, an object file, a binary...).
struct Task {
    std::string name;
    long long cost = 0;       // build time in ms
    std::vector<int> deps;    // ids of tasks that must finish before this one
    std::vector<int> users;   // ids of tasks that wait for this one (reverse edges)
};

// Dependency graph stored as two adjacency lists (deps and users). Keeping the reverse
// edges around costs a bit of memory but makes "what breaks if X changes" a plain BFS.
class Graph {
public:
    // Returns the new id, or -1 if a task with this name already exists.
    int addTask(const std::string& name, long long cost);

    // -1 if there is no such task.
    int find(const std::string& name) const;

    // Makes `task` wait for `dep`. Does not check for duplicates, the caller must not add
    // the same edge twice (the parser takes care of that).
    void addEdge(int task, int dep);

    int size() const { return static_cast<int>(tasks_.size()); }
    std::size_t edgeCount() const { return edges_; }
    const Task& at(int id) const { return tasks_[id]; }
    const std::string& name(int id) const { return tasks_[id].name; }
    long long cost(int id) const { return tasks_[id].cost; }
    long long totalWork() const;

private:
    std::vector<Task> tasks_;
    std::unordered_map<std::string, int> index_;
    std::size_t edges_ = 0;
};

}  // namespace kiln
