#include "mcmf.hpp"

#include <algorithm>
#include <limits>
#include <stdexcept>

namespace foodloop {
namespace {

const long long UNREACHED = std::numeric_limits<long long>::max();

}  // namespace

int MinCostMaxFlow::addNode() {
    adj_.push_back(std::vector<Arc>());
    return static_cast<int>(adj_.size()) - 1;
}

int MinCostMaxFlow::nodeCount() const { return static_cast<int>(adj_.size()); }

int MinCostMaxFlow::addEdge(int u, int v, long long capacity, long long cost) {
    if (u < 0 || v < 0 || u >= nodeCount() || v >= nodeCount()) throw std::out_of_range("addEdge: bad node");
    if (u == v) throw std::invalid_argument("addEdge: self-loop");
    if (capacity < 0) throw std::invalid_argument("addEdge: negative capacity");

    Arc forward = {v, static_cast<int>(adj_[v].size()), capacity, cost, capacity};
    Arc backward = {u, static_cast<int>(adj_[u].size()), 0, -cost, 0};
    adj_[u].push_back(forward);
    adj_[v].push_back(backward);

    edges_.push_back(std::make_pair(u, static_cast<int>(adj_[u].size()) - 1));
    return static_cast<int>(edges_.size()) - 1;
}

long long MinCostMaxFlow::flowOn(int edgeId) const {
    const Arc& a = adj_[edges_.at(edgeId).first][edges_.at(edgeId).second];
    return a.original - a.cap;
}

bool MinCostMaxFlow::bellmanFord(int source, int sink, std::vector<long long>& dist, std::vector<int>& prevNode,
                                 std::vector<int>& prevArc) const {
    const int n = nodeCount();
    dist.assign(n, UNREACHED);
    prevNode.assign(n, -1);
    prevArc.assign(n, -1);
    dist[source] = 0;

    // At most V-1 rounds of relaxing every residual arc; stop early once nothing changes.
    for (int round = 0; round < n - 1; ++round) {
        bool changed = false;
        for (int u = 0; u < n; ++u) {
            if (dist[u] == UNREACHED) continue;
            for (size_t i = 0; i < adj_[u].size(); ++i) {
                const Arc& a = adj_[u][i];
                if (a.cap > 0 && dist[u] + a.cost < dist[a.to]) {
                    dist[a.to] = dist[u] + a.cost;
                    prevNode[a.to] = u;
                    prevArc[a.to] = static_cast<int>(i);
                    changed = true;
                }
            }
        }
        if (!changed) break;
    }
    return dist[sink] != UNREACHED;
}

MinCostMaxFlow::Result MinCostMaxFlow::solve(int source, int sink) {
    Result r = {0, 0, 0};
    std::vector<long long> dist;
    std::vector<int> prevNode, prevArc;

    while (bellmanFord(source, sink, dist, prevNode, prevArc)) {
        // Bottleneck: the smallest residual capacity along the path.
        long long push = std::numeric_limits<long long>::max();
        for (int v = sink; v != source; v = prevNode[v]) push = std::min(push, adj_[prevNode[v]][prevArc[v]].cap);

        // Push it: use up forward capacity, give the same amount to the twin arcs.
        for (int v = sink; v != source; v = prevNode[v]) {
            Arc& a = adj_[prevNode[v]][prevArc[v]];
            a.cap -= push;
            adj_[v][a.rev].cap += push;
        }

        r.flow += push;
        r.cost += push * dist[sink];
        ++r.augmentations;
    }
    return r;
}

}  // namespace foodloop
