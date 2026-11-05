// Minimum Cost Maximum Flow by Successive Shortest Augmenting Paths.
//
// Every edge u -> v is stored with a residual twin v -> u (capacity 0, cost -cost).
// Pushing flow on an edge frees capacity on its twin, which lets a later path
// "undo" an earlier choice. Because twins have negative cost, the cheapest path
// is found with Bellman-Ford rather than Dijkstra.
#pragma once

#include <utility>
#include <vector>

namespace foodloop {

class MinCostMaxFlow {
public:
    int addNode();  // returns the new node's index
    int nodeCount() const;

    // Adds a directed edge with its residual twin. Returns an edge id for flowOn().
    int addEdge(int u, int v, long long capacity, long long cost);

    struct Result {
        long long flow;     // maximum flow from source to sink
        long long cost;     // minimum total cost among all maximum flows
        int augmentations;  // number of shortest paths pushed
    };

    // Runs the algorithm. O(F * V * E), F = total flow pushed.
    Result solve(int source, int sink);

    long long flowOn(int edgeId) const;

private:
    struct Arc {
        int to;
        int rev;            // index of the twin arc in adj_[to]
        long long cap;      // remaining (residual) capacity
        long long cost;
        long long original; // capacity when added; flow = original - cap
    };

    // Cheapest source -> sink path in the residual graph, or false if none is left.
    bool bellmanFord(int source, int sink, std::vector<long long>& dist, std::vector<int>& prevNode,
                     std::vector<int>& prevArc) const;

    std::vector<std::vector<Arc>> adj_;
    std::vector<std::pair<int, int>> edges_;  // edge id -> (node, index in adj_[node])
};

}  // namespace foodloop
