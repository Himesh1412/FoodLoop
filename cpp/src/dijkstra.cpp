#include "dijkstra.hpp"

#include <algorithm>
#include <functional>
#include <queue>
#include <stdexcept>
#include <utility>

namespace foodloop {

ShortestPaths dijkstra(const Graph& g, int source) {
    ShortestPaths sp;
    sp.source = source;
    sp.dist.assign(g.size(), INF);
    sp.parent.assign(g.size(), -1);

    // Min-heap of (distance so far, junction): top() is always the closest junction.
    typedef std::pair<double, int> Item;
    std::priority_queue<Item, std::vector<Item>, std::greater<Item>> pq;

    sp.dist[source] = 0.0;
    pq.push(Item(0.0, source));

    while (!pq.empty()) {
        const Item top = pq.top();
        pq.pop();
        const int u = top.second;

        // A junction can be pushed more than once when its distance improves.
        // Older, longer copies are stale: skip them ("lazy deletion").
        if (top.first > sp.dist[u]) continue;

        // Relaxation: is going through u a shorter way to reach v?
        for (const Edge& e : g.neighbours(u)) {
            const double candidate = sp.dist[u] + e.weight;
            if (candidate < sp.dist[e.to]) {
                sp.dist[e.to] = candidate;
                sp.parent[e.to] = u;
                pq.push(Item(candidate, e.to));
            }
        }
    }
    return sp;
}

std::vector<int> reconstructPath(const ShortestPaths& sp, int target) {
    std::vector<int> path;
    if (sp.dist.at(target) == INF) return path;
    for (int v = target; v != -1; v = sp.parent[v]) path.push_back(v);
    std::reverse(path.begin(), path.end());
    return path;
}

std::vector<int> DistanceMatrix::route(int donor, int ngo) const {
    return reconstructPath(runs.at(donor), ngoNodes.at(ngo));
}

DistanceMatrix computeDistanceMatrix(const Dataset& data, const Graph& g) {
    DistanceMatrix m;

    for (const Ngo& n : data.ngos) {
        const int node = g.indexOf(n.node_id);
        if (node < 0) throw std::runtime_error("NGO " + n.id + " is at unknown junction '" + n.node_id + "'");
        m.ngoIds.push_back(n.id);
        m.ngoNodes.push_back(node);
    }

    for (const Donor& d : data.donors) {
        const int node = g.indexOf(d.node_id);
        if (node < 0) throw std::runtime_error("donor " + d.id + " is at unknown junction '" + d.node_id + "'");

        const ShortestPaths sp = dijkstra(g, node);
        std::vector<double> row;
        for (int ngoNode : m.ngoNodes) row.push_back(sp.dist[ngoNode]);

        m.donorIds.push_back(d.id);
        m.km.push_back(row);
        m.runs.push_back(sp);
    }
    return m;
}

}  // namespace foodloop
