#include "kiln/models/graph.hpp"

namespace kiln {

int Graph::addTask(const std::string& name, long long cost) {
    if (index_.count(name)) return -1;
    int id = static_cast<int>(tasks_.size());
    Task t;
    t.name = name;
    t.cost = cost;
    tasks_.push_back(std::move(t));
    index_.emplace(name, id);
    return id;
}

int Graph::find(const std::string& name) const {
    auto it = index_.find(name);
    return it == index_.end() ? -1 : it->second;
}

void Graph::addEdge(int task, int dep) {
    tasks_[task].deps.push_back(dep);
    tasks_[dep].users.push_back(task);
    edges_++;
}

long long Graph::totalWork() const {
    long long sum = 0;
    for (const Task& t : tasks_) sum += t.cost;
    return sum;
}

}  // namespace kiln
