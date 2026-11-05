// Road network stored as an adjacency list.
// Junction ids such as "J10" are mapped to integer indices 0..V-1 so the
// algorithms can use plain vectors instead of string lookups.
#pragma once

#include <map>
#include <string>
#include <vector>

#include "data.hpp"

namespace foodloop {

struct Edge {
    int to;
    double weight;  // road length in km
};

class Graph {
public:
    // Adds a junction and returns its index. Adding an existing id returns its old index.
    int addNode(const std::string& id);

    // Adds a two-way road. Both junctions must already exist and the length must be >= 0,
    // because Dijkstra is only correct without negative weights.
    void addUndirectedEdge(const std::string& u, const std::string& v, double weight);

    int indexOf(const std::string& id) const;  // -1 if the id is unknown
    const std::string& idOf(int index) const;
    int size() const;
    int edgeCount() const;  // number of roads (each two-way road counted once)
    const std::vector<Edge>& neighbours(int index) const;

private:
    std::vector<std::vector<Edge>> adj_;
    std::vector<std::string> ids_;
    std::map<std::string, int> index_;
    int edgeCount_ = 0;
};

// Builds the road graph from nodes.csv and roads.csv.
Graph buildRoadGraph(const Dataset& data);

}  // namespace foodloop
