// Verification tests: dataset validation, cost formula, Greedy baseline and metric formulas.
//
//   test_eval [project_root]      (default: current directory)

#include <cmath>
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
#include "metrics.hpp"
#include "trips.hpp"

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

const Params kParams = {0.5, {6, 24}, 10, {3, 2, 1}, 8, 20, 0.5};

struct Pipeline {
    Dataset data;
    Feasibility feas;
    AllocationPlan mcmf, greedy;
    Metrics mcmfMetrics, greedyMetrics;
};

Pipeline run(const Dataset& data) {
    Pipeline p;
    p.data = data;
    const Graph g = buildRoadGraph(p.data);
    const DistanceMatrix m = computeDistanceMatrix(p.data, g);
    p.feas = computeFeasibility(p.data, m, kParams);
    p.mcmf = allocateByTiers(p.data, p.feas, kParams);
    p.greedy = allocateGreedy(p.data, p.feas, kParams);
    p.mcmfMetrics = computeMetrics(p.mcmf, planTrips(p.data, p.mcmf, kParams), kParams, 0.0);
    p.greedyMetrics = computeMetrics(p.greedy, planTrips(p.data, p.greedy, kParams), kParams, 0.0);
    return p;
}

long long allocated(const Pipeline& p, const AllocationPlan& plan, const std::string& lot, const std::string& ngo) {
    long long kg = 0;
    for (const Allocation& a : plan.allocations)
        if (p.data.packages[a.lot].id == lot && p.data.ngos[a.ngo].id == ngo) kg += a.kg;
    return kg;
}

bool rejects(const Dataset& d) {
    try {
        validateDataset(d);
    } catch (const std::runtime_error&) {
        return true;
    }
    return false;
}

void testValidation(const std::string& root) {
    std::cout << "dataset validation rejects bad values\n";
    const Dataset good = loadDataset(root + "/data/demo");
    CHECK(!rejects(good));

    Dataset d = good;
    d.vehicles[0].speed_kmph = 0;  // would divide by zero in travel time
    CHECK(rejects(d));

    d = good;
    d.vehicles[0].capacity_kg = 0;
    CHECK(rejects(d));

    d = good;
    d.packages[0].weight_kg = 0;  // supply must be positive
    CHECK(rejects(d));

    d = good;
    d.packages[0].expiry_hours = -1;
    CHECK(rejects(d));

    d = good;
    d.ngos[0].demand_kg["VEG"] = -5;
    CHECK(rejects(d));

    d = good;
    d.packages[1].id = d.packages[0].id;  // duplicate lot id
    CHECK(rejects(d));

    d = good;
    d.packages[0].donor_id = "D99";
    CHECK(rejects(d));

    d = good;
    d.ngos[0].node_id = "J99";
    CHECK(rejects(d));
}

void testExpiredLotExcluded(const std::string& root) {
    std::cout << "a lot with no shelf life left is never allocated\n";
    Dataset d = loadDataset(root + "/data/demo");
    d.packages[0].expiry_hours = 0;  // P1 has expired
    const Pipeline p = run(d);
    CHECK(p.feas.expiredLots == 1);
    CHECK(p.feas.byLot[0].empty());
    CHECK(p.mcmf.unplacedKg[0] == d.packages[0].weight_kg);
    CHECK(p.greedy.unplacedKg[0] == d.packages[0].weight_kg);
}

// TC = sum x_ij * D_ij * R, quantity counted once.
void testCostFormula(const std::string& root) {
    std::cout << "TC = sum x * D * R (quantity applied once)\n";
    const Pipeline p = run(loadDataset(root + "/data/demo"));
    double tcRupees = 0;
    for (const Allocation& a : p.mcmf.allocations) {
        tcRupees += a.kg * a.km * kParams.rate_inr_per_kg_km;
        CHECK(a.cost_paise == a.kg * static_cast<long long>(std::floor(a.km * kParams.rate_inr_per_kg_km * 100 + 0.5)));
    }
    CHECK(std::fabs(p.mcmfMetrics.allocationCostPaise / 100.0 - tcRupees) < 0.01 * p.mcmf.allocations.size());
    CHECK(p.mcmfMetrics.allocationCostPaise == p.mcmf.costPaise);

    long long tierSum = 0;
    for (const TierResult& t : p.mcmf.tiers) tierSum += t.costPaise;
    CHECK(tierSum == p.mcmf.costPaise);  // solver cost equals the sum over allocations
}

void testGreedyBaselineToy(const std::string& root) {
    std::cout << "greedy baseline on the toy: TP1 -> nearest TN1, TP2 stranded\n";
    const Pipeline p = run(loadDataset(root + "/data/toy"));
    CHECK(allocated(p, p.greedy, "TP1", "TN1") == 8);
    CHECK(allocated(p, p.greedy, "TP2", "TN1") == 0);
    CHECK(p.greedy.allocatedKg == 8);
    CHECK(p.greedy.costPaise == 400);  // 8 kg * 1 km * Rs 0.50

    CHECK(p.mcmf.allocatedKg == 16);
    CHECK(near(p.greedyMetrics.demandFulfilledPct, 50.0));
    CHECK(near(p.mcmfMetrics.demandFulfilledPct, 100.0));
    CHECK(p.greedyMetrics.unallocatedSurplusKg == 8);
    CHECK(p.mcmfMetrics.unallocatedSurplusKg == 0);
}

void testGreedyBaselineRespectsConstraints(const std::string& root) {
    std::cout << "greedy baseline uses the same constraints as MCMF\n";
    const Pipeline p = run(loadDataset(root + "/data/demo"));
    std::vector<long long> sent(p.data.packages.size(), 0);
    std::vector<std::map<std::string, long long>> got(p.data.ngos.size());
    for (const Allocation& a : p.greedy.allocations) {
        bool feasible = false;
        for (const LotRoute& r : p.feas.byLot[a.lot]) feasible = feasible || r.ngo == a.ngo;
        CHECK(feasible);
        sent[a.lot] += a.kg;
        got[a.ngo][p.data.packages[a.lot].category] += a.kg;
    }
    for (size_t k = 0; k < sent.size(); ++k) CHECK(sent[k] <= p.data.packages[k].weight_kg);
    for (size_t j = 0; j < got.size(); ++j)
        for (const std::string& cat : categories()) CHECK(got[j][cat] <= p.data.ngos[j].demandFor(cat));

    // Holds on the demo. It is guaranteed only within a single tier; across tiers see T6.21-T6.22.
    CHECK(p.mcmf.allocatedKg >= p.greedy.allocatedKg);
}

void testMetricFormulas(const std::string& root) {
    std::cout << "DF, US and EF formulas\n";
    const Pipeline p = run(loadDataset(root + "/data/demo"));
    const Metrics& m = p.mcmfMetrics;
    CHECK(m.supplyKg == 206);
    CHECK(m.demandKg == 200);
    CHECK(near(m.demandFulfilledPct, 100.0 * m.allocatedKg / m.demandKg));
    CHECK(m.unallocatedSurplusKg == m.supplyKg - m.allocatedKg);
    CHECK(near(m.expiryFeasiblePct, 100.0 * m.deliveredKg / m.allocatedKg));
    CHECK(m.deliveredKg + m.undeliveredKg == m.allocatedKg);
    CHECK(m.allocatedKg == 186);  // per category min(supply, demand): 100 + 45 + 41
}

}  // namespace

int main(int argc, char** argv) {
    const std::string root = argc >= 2 ? argv[1] : ".";
    try {
        testValidation(root);
        testExpiredLotExcluded(root);
        testCostFormula(root);
        testGreedyBaselineToy(root);
        testGreedyBaselineRespectsConstraints(root);
        testMetricFormulas(root);
    } catch (const std::exception& e) {
        std::cerr << "error: " << e.what() << "\n";
        return 1;
    }

    std::cout << "\n" << (checks - failures) << "/" << checks << " checks passed\n";
    return failures == 0 ? 0 : 1;
}
