// Day 2 tests: graph, Dijkstra, distance matrix and greedy priority list.
// Expected values are worked out by hand in docs/problem_formulation.md and the Day 2 notes.
//
//   test_day2 [project_root]      (default: current directory)

#include <cmath>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

#include "data.hpp"
#include "dijkstra.hpp"
#include "graph.hpp"
#include "greedy.hpp"

using namespace foodloop;

namespace {

int checks = 0;
int failures = 0;

#define CHECK(cond)                                                                      \
    do {                                                                                 \
        ++checks;                                                                        \
        if (!(cond)) {                                                                   \
            ++failures;                                                                  \
            std::cerr << "  FAIL line " << __LINE__ << ": " << #cond << "\n";            \
        }                                                                                \
    } while (0)

bool near(double a, double b) { return std::fabs(a - b) < 1e-9; }

double distanceBetween(const Graph& g, const std::string& from, const std::string& to) {
    return dijkstra(g, g.indexOf(from)).dist[g.indexOf(to)];
}

std::vector<std::string> pathIds(const Graph& g, const std::vector<int>& path) {
    std::vector<std::string> ids;
    for (int v : path) ids.push_back(g.idOf(v));
    return ids;
}

double matrixAt(const DistanceMatrix& m, const std::string& donor, const std::string& ngo) {
    for (size_t d = 0; d < m.donorIds.size(); ++d)
        for (size_t n = 0; n < m.ngoIds.size(); ++n)
            if (m.donorIds[d] == donor && m.ngoIds[n] == ngo) return m.km[d][n];
    throw std::runtime_error("no matrix cell " + donor + "/" + ngo);
}

void testToyDistances(const std::string& root) {
    std::cout << "toy dataset distances\n";
    const Graph g = buildRoadGraph(loadDataset(root + "/data/toy"));

    CHECK(near(distanceBetween(g, "X", "X"), 0.0));
    CHECK(near(distanceBetween(g, "X", "Z"), 1.0));
    CHECK(near(distanceBetween(g, "X", "W"), 2.0));
    CHECK(near(distanceBetween(g, "Y", "Z"), 2.0));
    CHECK(near(distanceBetween(g, "Y", "W"), 5.0));  // no direct road: Y -> Z -> X -> W

    const ShortestPaths fromY = dijkstra(g, g.indexOf("Y"));
    const std::vector<std::string> expected = {"Y", "Z", "X", "W"};
    CHECK(pathIds(g, reconstructPath(fromY, g.indexOf("W"))) == expected);
}

void testDemoDistanceMatrix(const std::string& root) {
    std::cout << "demo dataset: D3 row of the distance matrix\n";
    const Dataset data = loadDataset(root + "/data/demo");
    const Graph g = buildRoadGraph(data);
    const DistanceMatrix m = computeDistanceMatrix(data, g);

    CHECK(m.donorIds.size() == 4);
    CHECK(m.ngoIds.size() == 5);
    CHECK(near(matrixAt(m, "D3", "N1"), 6.1));
    CHECK(near(matrixAt(m, "D3", "N2"), 9.3));
    CHECK(near(matrixAt(m, "D3", "N3"), 6.5));
    CHECK(near(matrixAt(m, "D3", "N4"), 3.0));
    CHECK(near(matrixAt(m, "D3", "N5"), 9.0));

    // D3 -> N1 goes through J9 (6.1 km), not J6 (6.2 km).
    const std::vector<std::string> expected = {"J10", "J9", "J5"};
    CHECK(pathIds(g, m.route(2, 0)) == expected);
}

void testUnreachable() {
    std::cout << "unreachable junction\n";
    Graph g;
    g.addNode("A");
    g.addNode("B");
    g.addNode("C");  // no roads touch C
    g.addUndirectedEdge("A", "B", 4.0);

    const ShortestPaths sp = dijkstra(g, g.indexOf("A"));
    CHECK(near(sp.dist[g.indexOf("B")], 4.0));
    CHECK(sp.dist[g.indexOf("C")] == INF);
    CHECK(reconstructPath(sp, g.indexOf("C")).empty());
}

void testBadRoadsRejected() {
    std::cout << "invalid roads are rejected\n";
    Graph g;
    g.addNode("A");
    g.addNode("B");

    bool threw = false;
    try { g.addUndirectedEdge("A", "B", -1.0); } catch (const std::runtime_error&) { threw = true; }
    CHECK(threw);

    threw = false;
    try { g.addUndirectedEdge("A", "Q", 1.0); } catch (const std::runtime_error&) { threw = true; }
    CHECK(threw);
}

void testGreedyOrder(const std::string& root) {
    std::cout << "greedy expiry priority list\n";
    const std::vector<Package> order = expiryPriorityList(loadDataset(root + "/data/demo").packages);

    // Ties: P14 (10 kg) before P2 (9 kg) at 3 h; P12 (20) before P1 (12) at 4 h;
    // P11, P9, P3 at 5 h; P8 before P7 at 6 h.
    const std::vector<std::string> expected = {"P10", "P14", "P2", "P12", "P1", "P11", "P9",
                                               "P3",  "P8",  "P7", "P6",  "P13", "P5", "P4"};
    std::vector<std::string> actual;
    for (const Package& p : order) actual.push_back(p.id);
    CHECK(actual == expected);
}

void testUrgencyTiers() {
    std::cout << "urgency tiers\n";
    const std::vector<double> b = {6, 24};
    CHECK(urgencyTier(0.75, b) == 0);
    CHECK(urgencyTier(5.99, b) == 0);
    CHECK(urgencyTier(6.0, b) == 1);  // boundary belongs to the later tier
    CHECK(urgencyTier(10, b) == 1);
    CHECK(urgencyTier(24, b) == 2);
    CHECK(urgencyTier(48, b) == 2);
    CHECK(urgencyTier(48, std::vector<double>()) == 0);  // no boundaries: single tier
}

}  // namespace

int main(int argc, char** argv) {
    const std::string root = argc >= 2 ? argv[1] : ".";
    try {
        testToyDistances(root);
        testDemoDistanceMatrix(root);
        testUnreachable();
        testBadRoadsRejected();
        testGreedyOrder(root);
        testUrgencyTiers();
    } catch (const std::exception& e) {
        std::cerr << "error: " << e.what() << "\n";
        return 1;
    }

    std::cout << "\n" << (checks - failures) << "/" << checks << " checks passed\n";
    return failures == 0 ? 0 : 1;
}
