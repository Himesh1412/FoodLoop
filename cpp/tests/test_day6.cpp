// Day 6 tests: supply-demand scenarios, category compatibility, vehicle capacity, edge cases,
// and randomised MCMF vs Greedy checks. Every scenario is built in memory with a known answer.
// Test ids (T6.x) match docs/testing.md.
//
//   test_day6

#include <cmath>
#include <iomanip>
#include <iostream>
#include <map>
#include <random>
#include <string>
#include <vector>

#include "allocation.hpp"
#include "data.hpp"
#include "dijkstra.hpp"
#include "feasibility.hpp"
#include "graph.hpp"
#include "greedy.hpp"
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

bool near(double a, double b) { return std::fabs(a - b) < 1e-6; }

const Params kParams = {0.5, {6, 24}, 10, {3, 2, 1}, 8, 20, 0.5};

// Builds a dataset in memory. Each donor gets one vehicle.
struct Builder {
    Dataset d;

    Builder& node(const std::string& id, double x = 0, double y = 0) {
        d.nodes.push_back(Node{id, x, y, id});
        return *this;
    }
    Builder& road(const std::string& u, const std::string& v, double km) {
        d.roads.push_back(Road{u, v, km});
        return *this;
    }
    Builder& donor(const std::string& id, const std::string& at, int capacityKg = 100, double speed = 30,
                   double costPerKm = 10) {
        d.donors.push_back(Donor{id, id, "restaurant", at});
        d.vehicles.push_back(Vehicle{"V" + id, id, "van", capacityKg, speed, costPerKm});
        return *this;
    }
    Builder& lot(const std::string& id, const std::string& donorId, const std::string& cat, int kg, double expiry) {
        d.packages.push_back(Package{id, donorId, id, cat, kg * 2, kg, expiry});
        return *this;
    }
    Builder& ngo(const std::string& id, const std::string& at, int veg, int nonVeg, int vegan) {
        Ngo n;
        n.id = n.name = id;
        n.type = "shelter";
        n.node_id = at;
        n.demand_kg["VEG"] = veg;
        n.demand_kg["NON_VEG"] = nonVeg;
        n.demand_kg["VEGAN"] = vegan;
        d.ngos.push_back(n);
        return *this;
    }
};

struct Run {
    Dataset data;
    Feasibility feas;
    AllocationPlan mcmf, greedy;
    DeliveryPlan delivery, greedyDelivery;
    Metrics m, g;
};

Run run(const Dataset& data, const Params& params = kParams) {
    validateDataset(data);
    Run r;
    r.data = data;
    const Graph graph = buildRoadGraph(data);
    const DistanceMatrix dm = computeDistanceMatrix(data, graph);
    r.feas = computeFeasibility(data, dm, params);
    r.mcmf = allocateByTiers(data, r.feas, params);
    r.greedy = allocateGreedy(data, r.feas, params);
    r.delivery = planTrips(data, r.mcmf, params);
    r.greedyDelivery = planTrips(data, r.greedy, params);
    r.m = computeMetrics(r.mcmf, r.delivery, params, 0);
    r.g = computeMetrics(r.greedy, r.greedyDelivery, params, 0);
    return r;
}

long long sent(const Run& r, const AllocationPlan& plan, const std::string& lot, const std::string& ngo) {
    long long kg = 0;
    for (const Allocation& a : plan.allocations)
        if (r.data.packages[a.lot].id == lot && r.data.ngos[a.ngo].id == ngo) kg += a.kg;
    return kg;
}

std::map<std::string, long long> undeliveredByReason(const Run& r) {
    std::map<std::string, long long> kg;
    for (const Undelivered& u : r.delivery.undelivered) kg[u.reason] += r.delivery.crates[u.crate].kg;
    return kg;
}

// Rules every allocation plan must obey, whichever method produced it.
bool planIsValid(const Run& r, const AllocationPlan& plan) {
    std::vector<long long> fromLot(r.data.packages.size(), 0);
    std::vector<std::map<std::string, long long>> toNgo(r.data.ngos.size());
    for (const Allocation& a : plan.allocations) {
        bool feasible = false;
        for (const LotRoute& lr : r.feas.byLot[a.lot]) feasible = feasible || lr.ngo == a.ngo;
        if (!feasible || a.kg <= 0) return false;
        fromLot[a.lot] += a.kg;
        toNgo[a.ngo][r.data.packages[a.lot].category] += a.kg;
    }
    for (size_t k = 0; k < fromLot.size(); ++k)
        if (fromLot[k] > r.data.packages[k].weight_kg) return false;
    for (size_t j = 0; j < toNgo.size(); ++j)
        for (const std::string& c : categories())
            if (toNgo[j][c] > r.data.ngos[j].demandFor(c)) return false;
    return true;
}

// Donor D at A, NGO N at B, 2 km apart; one VEG lot.
Builder twoPoint(int supplyKg, int demandKg) {
    Builder b;
    b.node("A").node("B").road("A", "B", 2).donor("D", "A").lot("L", "D", "VEG", supplyKg, 5).ngo("N", "B", demandKg, 0, 0);
    return b;
}

// ------------------------------------------------------------------ supply and demand

void testSupplyDemand() {
    std::cout << "T6.1-T6.5 supply-demand scenarios\n";
    Run r = run(twoPoint(30, 20).d);  // T6.1 surplus
    CHECK(r.m.allocatedKg == 20 && r.m.unallocatedSurplusKg == 10 && near(r.m.demandFulfilledPct, 100));
    CHECK(r.mcmf.unmetKg[0].at("VEG") == 0);

    r = run(twoPoint(30, 50).d);  // T6.2 shortage
    CHECK(r.m.allocatedKg == 30 && r.m.unallocatedSurplusKg == 0 && near(r.m.demandFulfilledPct, 60));
    CHECK(r.mcmf.unmetKg[0].at("VEG") == 20);

    r = run(twoPoint(30, 30).d);  // T6.3 balanced
    CHECK(r.m.allocatedKg == 30 && r.m.unallocatedSurplusKg == 0 && near(r.m.demandFulfilledPct, 100));

    r = run(twoPoint(30, 0).d);  // T6.4 no demand at all
    CHECK(r.m.allocatedKg == 0 && r.m.trips == 0 && r.m.unallocatedSurplusKg == 30);
    CHECK(r.feas.keptPairs == 0 && r.feas.removedIncompatible == 1);

    Builder b;  // T6.5 no food lots
    b.node("A").node("B").road("A", "B", 2).donor("D", "A").ngo("N", "B", 10, 0, 0);
    r = run(b.d);
    CHECK(r.m.supplyKg == 0 && r.m.allocatedKg == 0 && r.m.trips == 0 && near(r.m.demandFulfilledPct, 0));
}

// ------------------------------------------------------------------ categories

void testCategories() {
    std::cout << "T6.6-T6.8 food compatibility\n";
    Builder b;  // T6.6 nothing compatible: non-veg food, vegan-only NGOs
    b.node("A").node("B").node("C").road("A", "B", 2).road("B", "C", 2).donor("D", "A")
        .lot("L1", "D", "NON_VEG", 10, 5).lot("L2", "D", "NON_VEG", 5, 5)
        .ngo("N1", "B", 0, 0, 20).ngo("N2", "C", 0, 0, 20);
    Run r = run(b.d);
    CHECK(r.feas.removedIncompatible == 4 && r.feas.keptPairs == 0);
    CHECK(r.m.allocatedKg == 0 && r.g.allocatedKg == 0 && r.m.unallocatedSurplusKg == 15);

    Builder c;  // T6.7 categories match exactly: vegan food does not fill VEG demand
    c.node("A").node("B").road("A", "B", 2).donor("D", "A").lot("L", "D", "VEGAN", 10, 5).ngo("N", "B", 10, 0, 0);
    r = run(c.d);
    CHECK(r.m.allocatedKg == 0 && r.mcmf.unmetKg[0].at("VEG") == 10);

    Builder e;  // T6.8 non-veg food only reaches the NGO that accepts it, even though it is farther
    e.node("A").node("B").node("C").road("A", "B", 1).road("A", "C", 5).donor("D", "A")
        .lot("NV", "D", "NON_VEG", 10, 5).lot("V", "D", "VEG", 10, 5)
        .ngo("VegOnly", "B", 10, 0, 0).ngo("All", "C", 10, 10, 0);
    r = run(e.d);
    CHECK(sent(r, r.mcmf, "NV", "All") == 10 && sent(r, r.mcmf, "NV", "VegOnly") == 0);
    CHECK(sent(r, r.mcmf, "V", "VegOnly") == 10);  // the cheaper veg destination
    CHECK(r.m.allocatedKg == 20);
}

// ------------------------------------------------------------------ vehicle capacity

// One donor, one NGO 1 km away, vehicle at 30 km/h: travel 1/30 h, served at +0.533 h, back at +0.567 h.
Builder oneKm(int capacityKg, int lotKg, double expiry) {
    Builder b;
    b.node("A").node("B").road("A", "B", 1).donor("D", "A", capacityKg).lot("L", "D", "VEG", lotKg, expiry)
        .ngo("N", "B", 100, 0, 0);
    return b;
}

void testVehicleCapacity() {
    std::cout << "T6.9-T6.12 insufficient vehicle capacity\n";
    Run r = run(oneKm(5, 8, 5).d);  // T6.9 crate (8 kg) heavier than the vehicle (5 kg)
    CHECK(r.m.allocatedKg == 8 && r.m.deliveredKg == 0 && undeliveredByReason(r)["too heavy"] == 8);
    CHECK(near(r.m.expiryFeasiblePct, 0));

    r = run(oneKm(10, 30, 1.0).d);  // T6.10 small vehicle, short shelf life: later trips are too late
    CHECK(r.m.trips == 1 && r.m.deliveredKg == 10 && undeliveredByReason(r)["expired"] == 20);
    CHECK(near(r.m.expiryFeasiblePct, 100.0 / 3));

    r = run(oneKm(10, 30, 10).d);  // T6.11 small vehicle, long shelf life: three trips deliver it all
    CHECK(r.m.trips == 3 && r.m.deliveredKg == 30 && r.m.undeliveredKg == 0);
    CHECK(r.delivery.trips.size() == 3 && near(r.delivery.trips[2].departHours, 2 * (2.0 / 30 + 0.5)));

    Params shortShift = kParams;  // T6.12 the shift ends before the second trip is served (1.1 h > 1 h)
    shortShift.shift_hours = 1.0;
    r = run(oneKm(10, 30, 10).d, shortShift);
    CHECK(r.m.deliveredKg == 10 && undeliveredByReason(r)["shift over"] == 20);

    // Vehicle capacity is not part of the flow network: allocation is the same for any capacity.
    CHECK(run(oneKm(5, 30, 10).d).m.allocatedKg == run(oneKm(500, 30, 10).d).m.allocatedKg);
}

// ------------------------------------------------------------------ expiry, reachability, geometry

void testEdgeCases() {
    std::cout << "T6.13-T6.17 edge cases\n";
    // T6.13 expiry boundary is inclusive: 3 km at 30 km/h = 0.1 h, + 0.5 h handling = 0.6 h.
    Builder b;
    b.node("A").node("B").road("A", "B", 3).donor("D", "A").lot("OnTime", "D", "VEG", 5, 0.6)
        .lot("Late", "D", "VEG", 5, 0.59).ngo("N", "B", 20, 0, 0);
    Run r = run(b.d);
    CHECK(sent(r, r.mcmf, "OnTime", "N") == 5 && sent(r, r.mcmf, "Late", "N") == 0);
    CHECK(r.feas.removedExpiry == 1);

    Builder c;  // T6.14 food already expired at planning time
    c.node("A").node("B").road("A", "B", 1).donor("D", "A").lot("L", "D", "VEG", 10, 0).ngo("N", "B", 10, 0, 0);
    r = run(c.d);
    CHECK(r.feas.expiredLots == 1 && r.m.allocatedKg == 0 && r.g.allocatedKg == 0);

    Builder e;  // T6.15 NGO on a junction with no roads
    e.node("A").node("B").node("Island").road("A", "B", 1).donor("D", "A").lot("L", "D", "VEG", 10, 5)
        .ngo("Near", "B", 4, 0, 0).ngo("Cut", "Island", 10, 0, 0);
    r = run(e.d);
    CHECK(r.feas.removedUnreachable == 1 && sent(r, r.mcmf, "L", "Cut") == 0);
    CHECK(r.m.allocatedKg == 4 && r.mcmf.unmetKg[1].at("VEG") == 10);

    Builder f;  // T6.16 donor and NGO on the same junction: zero distance, zero cost
    f.node("A").donor("D", "A").lot("L", "D", "VEG", 10, 1).ngo("N", "A", 10, 0, 0);
    r = run(f.d);
    CHECK(r.m.allocatedKg == 10 && r.m.allocationCostPaise == 0 && r.m.tripCostPaise == 0 && r.m.deliveredKg == 10);

    Builder g;  // T6.17 tiers: the urgent lot claims the only demand, the long-life lot waits
    g.node("A").node("B").road("A", "B", 2).donor("D", "A").lot("Urgent", "D", "VEG", 10, 3)
        .lot("LongLife", "D", "VEG", 10, 30).ngo("N", "B", 10, 0, 0);
    r = run(g.d);
    CHECK(sent(r, r.mcmf, "Urgent", "N") == 10 && sent(r, r.mcmf, "LongLife", "N") == 0);
    CHECK(sent(r, r.greedy, "Urgent", "N") == 10);
}

// ------------------------------------------------------------------ Greedy vs MCMF

void testGreedyVsMcmf() {
    std::cout << "T6.18-T6.20 Greedy vs MCMF\n";
    // T6.18 Greedy strands food (same structure as the toy): A expires first, takes the nearest NGO N1;
    // B can only reach N1 in time.
    Builder b;
    b.node("X").node("Y").node("Z").node("W").road("X", "Z", 1).road("X", "W", 2).road("Y", "Z", 2)
        .donor("DA", "X", 50, 30).donor("DB", "Y", 50, 5)
        .lot("A", "DA", "VEG", 8, 1.0).lot("B", "DB", "VEG", 8, 1.4).ngo("N1", "Z", 8, 0, 0).ngo("N2", "W", 8, 0, 0);
    Run r = run(b.d);
    CHECK(r.m.allocatedKg == 16 && r.g.allocatedKg == 8);

    // T6.19 Same quantity, Greedy pays more. Line N2 -1- D1 -1- N1 -1- D2: Greedy sends A (more urgent)
    // to N1 (tie, listed first), so B must travel 3 km to N2. MCMF sends A -> N2, B -> N1.
    Builder c;
    c.node("N2").node("D1").node("N1").node("D2").road("N2", "D1", 1).road("D1", "N1", 1).road("N1", "D2", 1)
        .donor("DA", "D1").donor("DB", "D2").lot("A", "DA", "VEG", 10, 2).lot("B", "DB", "VEG", 10, 4)
        .ngo("N1", "N1", 10, 0, 0).ngo("N2", "N2", 10, 0, 0);
    r = run(c.d);
    CHECK(r.m.allocatedKg == 20 && r.g.allocatedKg == 20);
    CHECK(r.m.allocationCostPaise == 1000 && r.g.allocationCostPaise == 2000);  // 20 kg*km vs 40 kg*km at Rs 0.50

    // T6.20 Determinism: the same input always gives the same plan.
    const Run again = run(c.d);
    CHECK(again.mcmf.allocations.size() == r.mcmf.allocations.size());
    for (size_t i = 0; i < r.mcmf.allocations.size() && i < again.mcmf.allocations.size(); ++i)
        CHECK(again.mcmf.allocations[i].lot == r.mcmf.allocations[i].lot &&
              again.mcmf.allocations[i].ngo == r.mcmf.allocations[i].ngo &&
              again.mcmf.allocations[i].kg == r.mcmf.allocations[i].kg);
}

// ------------------------------------------------------------------ randomised checks

Dataset randomInstance(std::mt19937& rng) {
    auto uni = [&](int lo, int hi) { return std::uniform_int_distribution<int>(lo, hi)(rng); };
    auto real = [&](double lo, double hi) { return std::uniform_real_distribution<double>(lo, hi)(rng); };
    const char* cats[] = {"VEG", "NON_VEG", "VEGAN"};

    Builder b;
    const int nodes = uni(4, 9);
    std::vector<std::pair<double, double>> xy;
    for (int i = 0; i < nodes; ++i) {
        xy.push_back(std::make_pair(real(0, 10), real(0, 10)));
        b.node("J" + std::to_string(i), xy.back().first, xy.back().second);
    }
    auto km = [&](int i, int j) {
        return std::ceil(std::hypot(xy[i].first - xy[j].first, xy[i].second - xy[j].second) * 11) / 10 + 0.1;
    };
    for (int i = 1; i < nodes; ++i) {  // random spanning tree keeps it connected, plus a few extra roads
        const int j = uni(0, i - 1);
        b.road("J" + std::to_string(i), "J" + std::to_string(j), km(i, j));
    }
    for (int e = uni(0, nodes); e > 0; --e) {
        const int i = uni(0, nodes - 1), j = uni(0, nodes - 1);
        if (i != j) b.road("J" + std::to_string(i), "J" + std::to_string(j), km(i, j));
    }

    const int donors = uni(1, 3), ngos = uni(1, 4);
    for (int d = 0; d < donors; ++d)
        b.donor("D" + std::to_string(d), "J" + std::to_string(uni(0, nodes - 1)), uni(10, 80), real(5, 40));
    for (int k = uni(1, 7); k > 0; --k)
        b.lot("L" + std::to_string(k), "D" + std::to_string(uni(0, donors - 1)), cats[uni(0, 2)], uni(1, 25),
              std::floor(real(0.25, 30) * 4) / 4);
    for (int n = 0; n < ngos; ++n)
        b.ngo("N" + std::to_string(n), "J" + std::to_string(uni(0, nodes - 1)), uni(0, 25), uni(0, 25), uni(0, 25));
    return b.d;
}

// T6.21 With a single tier, MCMF solves one max-flow min-cost problem, so it can never allocate less
// than Greedy, and when both allocate the same amount MCMF is never more expensive.
// T6.22 With urgency tiers that guarantee no longer holds; report how often each method wins.
void testRandomised() {
    std::cout << "T6.21-T6.22 randomised MCMF vs Greedy (500 instances)\n";
    Params oneTier = kParams;
    oneTier.tier_boundaries_hours = {1000};  // every lot falls in tier 1
    oneTier.tier_weights = {1, 1};

    std::mt19937 rng(2026);
    int moreKg = 0, sameKgCheaper = 0, same = 0, tieredFewerKg = 0, invalid = 0;
    for (int i = 0; i < 500; ++i) {
        const Dataset d = randomInstance(rng);

        const Run single = run(d, oneTier);
        CHECK(single.m.allocatedKg >= single.g.allocatedKg);
        if (single.m.allocatedKg == single.g.allocatedKg)
            CHECK(single.m.allocationCostPaise <= single.g.allocationCostPaise);
        if (!planIsValid(single, single.mcmf) || !planIsValid(single, single.greedy)) ++invalid;
        CHECK(single.delivery.deliveredKg + single.delivery.undeliveredKg == single.mcmf.allocatedKg);

        if (single.m.allocatedKg > single.g.allocatedKg) ++moreKg;
        else if (single.m.allocationCostPaise < single.g.allocationCostPaise) ++sameKgCheaper;
        else ++same;

        const Run tiered = run(d, kParams);
        if (!planIsValid(tiered, tiered.mcmf) || !planIsValid(tiered, tiered.greedy)) ++invalid;
        if (tiered.m.allocatedKg < tiered.g.allocatedKg) ++tieredFewerKg;
    }
    CHECK(invalid == 0);
    std::cout << "    single tier: MCMF allocates more in " << moreKg << ", same kg but cheaper in "
              << sameKgCheaper << ", identical in " << same << " of 500\n"
              << "    with tiers:  MCMF allocates less than Greedy in " << tieredFewerKg << " of 500\n";
}

}  // namespace

int main() {
    try {
        testSupplyDemand();
        testCategories();
        testVehicleCapacity();
        testEdgeCases();
        testGreedyVsMcmf();
        testRandomised();
    } catch (const std::exception& e) {
        std::cerr << "error: " << e.what() << "\n";
        return 1;
    }
    std::cout << "\n" << (checks - failures) << "/" << checks << " checks passed\n";
    return failures == 0 ? 0 : 1;
}
