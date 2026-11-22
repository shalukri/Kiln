#include "kiln/services/scheduler.hpp"

#include <algorithm>
#include <functional>
#include <queue>
#include <stdexcept>

namespace kiln {

Schedule simulate(const Graph& g, const std::vector<long long>& bottomLevel, int workers, Policy policy) {
    if (workers < 1) throw std::invalid_argument("need at least one worker");
    const int n = g.size();
    if (policy == Policy::CriticalFirst && static_cast<int>(bottomLevel.size()) != n)
        throw std::invalid_argument("bottomLevel must have one entry per task");

    // extra workers beyond the task count can never be used, and a silly number like 10^9
    // would only waste memory in the free-worker heap
    workers = std::min(workers, std::max(n, 1));

    Schedule out;
    out.workers = workers;

    // ready queue: smallest key first, then smallest id
    struct Ready {
        long long key;
        int id;
    };
    auto readyAfter = [](const Ready& a, const Ready& b) {
        return a.key != b.key ? a.key > b.key : a.id > b.id;
    };
    std::priority_queue<Ready, std::vector<Ready>, decltype(readyAfter)> ready(readyAfter);
    long long arrival = 0;
    auto makeReady = [&](int id) {
        long long key = policy == Policy::Fifo ? arrival++ : -bottomLevel[id];
        ready.push({key, id});
    };

    // running tasks ordered by finish time
    struct Running {
        long long end;
        int id;
        int worker;
    };
    auto endsAfter = [](const Running& a, const Running& b) {
        return a.end != b.end ? a.end > b.end : a.id > b.id;
    };
    std::priority_queue<Running, std::vector<Running>, decltype(endsAfter)> running(endsAfter);

    std::priority_queue<int, std::vector<int>, std::greater<int>> freeWorkers;
    for (int w = 0; w < workers; w++) freeWorkers.push(w);

    std::vector<int> waiting(n);
    for (int i = 0; i < n; i++) {
        waiting[i] = static_cast<int>(g.at(i).deps.size());
        if (waiting[i] == 0) makeReady(i);
    }

    int finished = 0;
    long long now = 0, busy = 0;
    while (finished < n) {
        // hand out as much work as we can at the current time
        while (!freeWorkers.empty() && !ready.empty()) {
            Ready r = ready.top();
            ready.pop();
            int w = freeWorkers.top();
            freeWorkers.pop();
            long long end = now + g.cost(r.id);
            running.push({end, r.id, w});
            out.runs.push_back({r.id, w, now, end});
            busy += g.cost(r.id);
        }
        if (running.empty()) throw std::runtime_error("deadlock, the graph has a cycle");

        // jump to the next finish time and complete everything that ends exactly then
        now = running.top().end;
        while (!running.empty() && running.top().end == now) {
            Running r = running.top();
            running.pop();
            freeWorkers.push(r.worker);
            finished++;
            for (int v : g.at(r.id).users)
                if (--waiting[v] == 0) makeReady(v);
        }
    }

    out.makespan = now;
    out.utilization = now > 0 ? static_cast<double>(busy) / (static_cast<double>(workers) * static_cast<double>(now)) : 0.0;
    return out;
}

}  // namespace kiln
