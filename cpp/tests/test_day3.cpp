// Day 3 tests: Min Cost Max Flow solver, feasibility filter and tiered allocation.
// Expected values are worked out by hand in docs/problem_formulation.md sections 6 and 9.
//
//   test_day3 [project_root]      (default: current directory)

#include <iostream>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>

#include "allocation.hpp"
#include "data.hpp"
#include "dijkstra.hpp"
#include "feasibility.hpp"
#include "graph.hpp"
#include "greedy.hpp"
#include "mcmf.hpp"

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

const Params kParams = {0.5, {6, 24}, 10, {3, 2, 1}, 8, 20, 0.5};

struct Pipeline {
    Dataset data;
    Feasibility feas;
    AllocationPlan plan;
};

Pipeline run(const std::string& dir) {
    Pipeline p;
    p.data = loadDataset(dir);
    const Graph g = buildRoadGraph(p.data);
    const DistanceMatrix m = computeDistanceMatrix(p.data, g);
    p.feas = computeFeasibility(p.data, m, kParams);
    p.plan = allocateByTiers(p.data, p.feas, kParams);
    return p;
}

int lotIndex(const Dataset& d, const std::string& id) {
    for (size_t k = 0; k < d.packages.size(); ++k)
        if (d.packages[k].id == id) return static_cast<int>(k);
    throw std::runtime_error("no lot " + id);
}

std::vector<std::string> feasibleNgos(const Pipeline& p, const std::string& lotId) {
    std::vector<std::string> ids;
    for (const LotRoute& r : p.feas.byLot[lotIndex(p.data, lotId)]) ids.push_back(p.data.ngos[r.ngo].id);
    return ids;
}

long long allocated(const Pipeline& p, const std::string& lotId, const std::string& ngoId) {
    long long kg = 0;
    for (const Allocation& a : p.plan.allocations)
        if (p.data.packages[a.lot].id == lotId && p.data.ngos[a.ngo].id == ngoId) kg += a.kg;
    return kg;
}

// S -> a, S -> b; a -> z (cost 1), a -> w (cost 2), b -> z (cost 2); z, w -> T.
// The first shortest path takes a -> z. The only way to reach flow 2 is the second path
// S -> b -> z => a -> w -> T, which goes backwards over a -> z (residual cost -1).
void testMcmfNeedsReverseEdge() {
    std::cout << "MCMF undoes an earlier choice through a reverse edge\n";
    MinCostMaxFlow f;
    const int s = f.addNode(), t = f.addNode(), a = f.addNode(), b = f.addNode(), z = f.addNode(), w = f.addNode();
    f.addEdge(s, a, 1, 0);
    f.addEdge(s, b, 1, 0);
    const int az = f.addEdge(a, z, 1, 1);
    const int aw = f.addEdge(a, w, 1, 2);
    const int bz = f.addEdge(b, z, 1, 2);
    f.addEdge(z, t, 1, 0);
    f.addEdge(w, t, 1, 0);

    const MinCostMaxFlow::Result r = f.solve(s, t);
    CHECK(r.flow == 2);
    CHECK(r.cost == 4);  // a->w (2) + b->z (2)
    CHECK(r.augmentations == 2);
    CHECK(f.flowOn(az) == 0);
    CHECK(f.flowOn(aw) == 1);
    CHECK(f.flowOn(bz) == 1);
}

// S -> A (cap 5, cost 1), A -> T (cap 3, cost 1), S -> T (cap 4, cost 5).
// Max flow 7: 3 units cost 2 each, 4 units cost 5 each = 26.
void testMcmfPrefersCheapThenFillsExpensive() {
    std::cout << "MCMF fills the cheap path first, then the expensive one\n";
    MinCostMaxFlow f;
    const int s = f.addNode(), t = f.addNode(), a = f.addNode();
    f.addEdge(s, a, 5, 1);
    f.addEdge(a, t, 3, 1);
    f.addEdge(s, t, 4, 5);
    const MinCostMaxFlow::Result r = f.solve(s, t);
    CHECK(r.flow == 7);
    CHECK(r.cost == 26);
}

void testMcmfNoPath() {
    std::cout << "MCMF with no source-sink path\n";
    MinCostMaxFlow f;
    const int s = f.addNode(), t = f.addNode(), a = f.addNode();
    f.addEdge(s, a, 10, 1);
    const MinCostMaxFlow::Result r = f.solve(s, t);
    CHECK(r.flow == 0);
    CHECK(r.cost == 0);
}

void testMcmfMaxFlowBeatsCheapness() {
    std::cout << "MCMF never trades flow for lower cost\n";
    // A cheap path of capacity 1 and an expensive path of capacity 1: both must be used.
    MinCostMaxFlow f;
    const int s = f.addNode(), t = f.addNode(), a = f.addNode(), b = f.addNode();
    f.addEdge(s, a, 1, 0);
    f.addEdge(a, t, 1, 1);
    f.addEdge(s, b, 1, 0);
    f.addEdge(b, t, 1, 1000);
    const MinCostMaxFlow::Result r = f.solve(s, t);
    CHECK(r.flow == 2);
    CHECK(r.cost == 1001);
}

void testToyFeasibility(const std::string& root) {
    std::cout << "toy feasibility: TP2 cannot reach TN2 before expiry\n";
    const Pipeline p = run(root + "/data/toy");
    CHECK(feasibleNgos(p, "TP1") == std::vector<std::string>({"TN1", "TN2"}));
    CHECK(feasibleNgos(p, "TP2") == std::vector<std::string>({"TN1"}));
    CHECK(p.feas.removedExpiry == 1);

    // C_ij = D_ij * R with R = Rs 0.50 per kg-km: 1 km -> 50 paise/kg, 2 km -> 100 paise/kg.
    const std::vector<LotRoute>& tp1 = p.feas.byLot[lotIndex(p.data, "TP1")];
    CHECK(tp1[0].cost_paise_per_kg == 50);
    CHECK(tp1[1].cost_paise_per_kg == 100);
    CHECK(p.feas.byLot[lotIndex(p.data, "TP2")][0].cost_paise_per_kg == 100);
}

void testToyAllocation(const std::string& root) {
    std::cout << "toy allocation: MCMF allocates 16 kg for Rs 16\n";
    const Pipeline p = run(root + "/data/toy");
    CHECK(p.plan.allocatedKg == 16);
    CHECK(p.plan.costPaise == 1600);  // path 1: 8 kg * 50; path 2: 8 kg * (100 - 50 + 100)
    CHECK(allocated(p, "TP1", "TN2") == 8);
    CHECK(allocated(p, "TP2", "TN1") == 8);
    CHECK(allocated(p, "TP1", "TN1") == 0);
    CHECK(p.plan.tiers[0].augmentations == 2);  // second path uses the reverse edge
}

void testDemoFeasibility(const std::string& root) {
    std::cout << "demo feasibility rules\n";
    const Pipeline p = run(root + "/data/demo");
    CHECK(feasibleNgos(p, "P10") == std::vector<std::string>({"N4"}));        // khichdi: expiry
    CHECK(feasibleNgos(p, "P2") == std::vector<std::string>({"N1", "N2", "N3"}));  // non-veg: not N4, N5
    CHECK(feasibleNgos(p, "P4") == std::vector<std::string>({"N1", "N2", "N3", "N4", "N5"}));
    CHECK(p.feas.keptPairs + p.feas.removedIncompatible + p.feas.removedUnreachable + p.feas.removedExpiry ==
          14 * 5);
}

// Conservation checks that must hold for any correct plan.
void testDemoInvariants(const std::string& root) {
    std::cout << "demo allocation invariants\n";
    const Pipeline p = run(root + "/data/demo");
    const Dataset& d = p.data;

    CHECK(p.plan.supplyKg == 206);
    CHECK(p.plan.demandKg == 200);
    CHECK(p.plan.tiers.size() == 3);
    CHECK(p.plan.tiers[0].supplyKg == 122);
    CHECK(p.plan.tiers[1].supplyKg == 53);
    CHECK(p.plan.tiers[2].supplyKg == 31);

    std::vector<long long> sentFromLot(d.packages.size(), 0);
    std::vector<std::map<std::string, long long>> receivedByNgo(d.ngos.size());
    long long total = 0, cost = 0;
    for (const Allocation& a : p.plan.allocations) {
        const Package& lot = d.packages[a.lot];
        sentFromLot[a.lot] += a.kg;
        receivedByNgo[a.ngo][lot.category] += a.kg;
        total += a.kg;
        cost += a.cost_paise;
        CHECK(a.tier == urgencyTier(lot.expiry_hours, kParams.tier_boundaries_hours));
        bool feasible = false;
        for (const LotRoute& r : p.feas.byLot[a.lot]) feasible = feasible || r.ngo == a.ngo;
        CHECK(feasible);
    }
    for (size_t k = 0; k < d.packages.size(); ++k) {
        CHECK(sentFromLot[k] <= d.packages[k].weight_kg);
        CHECK(sentFromLot[k] + p.plan.unplacedKg[k] == d.packages[k].weight_kg);
    }
    for (size_t j = 0; j < d.ngos.size(); ++j) {
        for (const std::string& cat : categories()) {
            CHECK(receivedByNgo[j][cat] <= d.ngos[j].demandFor(cat));
            CHECK(receivedByNgo[j][cat] + p.plan.unmetKg[j].at(cat) == d.ngos[j].demandFor(cat));
        }
    }
    CHECK(total == p.plan.allocatedKg);
    CHECK(cost == p.plan.costPaise);

    // VEGAN demand is 55 kg but supply is only 41 kg, so at least 14 kg must stay unmet.
    long long veganUnmet = 0;
    for (size_t j = 0; j < d.ngos.size(); ++j) veganUnmet += p.plan.unmetKg[j].at("VEGAN");
    CHECK(veganUnmet >= 14);
}

}  // namespace

int main(int argc, char** argv) {
    const std::string root = argc >= 2 ? argv[1] : ".";
    try {
        testMcmfNeedsReverseEdge();
        testMcmfPrefersCheapThenFillsExpensive();
        testMcmfNoPath();
        testMcmfMaxFlowBeatsCheapness();
        testToyFeasibility(root);
        testToyAllocation(root);
        testDemoFeasibility(root);
        testDemoInvariants(root);
    } catch (const std::exception& e) {
        std::cerr << "error: " << e.what() << "\n";
        return 1;
    }

    std::cout << "\n" << (checks - failures) << "/" << checks << " checks passed\n";
    return failures == 0 ? 0 : 1;
}
