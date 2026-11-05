// Dijkstra's shortest paths and the donor -> NGO distance matrix.
#pragma once

#include <limits>
#include <string>
#include <vector>

#include "data.hpp"
#include "graph.hpp"

namespace foodloop {

const double INF = std::numeric_limits<double>::infinity();

// Result of one Dijkstra run from a single source junction.
struct ShortestPaths {
    int source;
    std::vector<double> dist;  // dist[v] = shortest km from source to v, INF if unreachable
    std::vector<int> parent;   // parent[v] = junction before v on that shortest path, -1 if none
};

// Dijkstra with a binary-heap priority queue. O((V + E) log V).
ShortestPaths dijkstra(const Graph& g, int source);

// Junctions from the source to target, inclusive. Empty if target is unreachable.
std::vector<int> reconstructPath(const ShortestPaths& sp, int target);

// Shortest road distance from every donor to every NGO.
struct DistanceMatrix {
    std::vector<std::string> donorIds;
    std::vector<std::string> ngoIds;
    std::vector<int> ngoNodes;              // junction index of each NGO
    std::vector<std::vector<double>> km;    // km[d][n], INF if unreachable
    std::vector<ShortestPaths> runs;        // one Dijkstra run per donor, kept for route lookup

    std::vector<int> route(int donor, int ngo) const;
};

// Runs Dijkstra once per donor. O(D * (V + E) log V).
DistanceMatrix computeDistanceMatrix(const Dataset& data, const Graph& g);

}  // namespace foodloop
