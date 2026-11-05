#include "graph.hpp"

#include <stdexcept>

namespace foodloop {

int Graph::addNode(const std::string& id) {
    std::map<std::string, int>::const_iterator it = index_.find(id);
    if (it != index_.end()) return it->second;
    const int idx = static_cast<int>(ids_.size());
    index_[id] = idx;
    ids_.push_back(id);
    adj_.push_back(std::vector<Edge>());
    return idx;
}

void Graph::addUndirectedEdge(const std::string& u, const std::string& v, double weight) {
    const int a = indexOf(u);
    const int b = indexOf(v);
    if (a < 0) throw std::runtime_error("road uses unknown junction '" + u + "'");
    if (b < 0) throw std::runtime_error("road uses unknown junction '" + v + "'");
    if (weight < 0) throw std::runtime_error("road " + u + "-" + v + " has negative length");

    Edge ab = {b, weight};
    Edge ba = {a, weight};
    adj_[a].push_back(ab);
    adj_[b].push_back(ba);
    ++edgeCount_;
}

int Graph::indexOf(const std::string& id) const {
    std::map<std::string, int>::const_iterator it = index_.find(id);
    return it == index_.end() ? -1 : it->second;
}

const std::string& Graph::idOf(int index) const { return ids_.at(index); }

int Graph::size() const { return static_cast<int>(ids_.size()); }

int Graph::edgeCount() const { return edgeCount_; }

const std::vector<Edge>& Graph::neighbours(int index) const { return adj_.at(index); }

Graph buildRoadGraph(const Dataset& data) {
    Graph g;
    for (const Node& n : data.nodes) g.addNode(n.id);
    for (const Road& r : data.roads) g.addUndirectedEdge(r.u, r.v, r.distance_km);
    return g;
}

}  // namespace foodloop
