// Day 4 tests: 0/1 Knapsack, crate splitting and the vehicle trip planner.
// Expected values are worked out by hand in docs/problem_formulation.md section 7.
//
//   test_day4 [project_root]      (default: current directory)

#include <algorithm>
#include <cmath>
#include <cstdlib>
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
#include "knapsack.hpp"
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
    AllocationPlan plan;
    DeliveryPlan delivery;
};

Pipeline run(const Dataset& data, const Params& params) {
    Pipeline p;
    p.data = data;
    const Graph g = buildRoadGraph(p.data);
    const DistanceMatrix m = computeDistanceMatrix(p.data, g);
    const Feasibility feas = computeFeasibility(p.data, m, params);
    p.plan = allocateByTiers(p.data, feas, params);
    p.delivery = planTrips(p.data, p.plan, params);
    return p;
}

std::vector<std::string> tripCrates(const Pipeline& p, const Trip& t) {
    std::vector<std::string> labels;
    for (int c : t.crates) labels.push_back(crateLabel(p.data, p.delivery.crates[c]));
    return labels;
}

std::vector<const Trip*> tripsOf(const Pipeline& p, const std::string& donorId) {
    std::vector<const Trip*> out;
    for (const Trip& t : p.delivery.trips)
        if (p.data.donors[t.donor].id == donorId) out.push_back(&t);
    return out;
}

// Greedy by value per kg, for the counterexample.
long long greedyByRatio(const std::vector<int>& w, const std::vector<long long>& v, int cap) {
    std::vector<int> order;
    for (size_t i = 0; i < w.size(); ++i) order.push_back(static_cast<int>(i));
    std::sort(order.begin(), order.end(), [&](int a, int b) { return v[a] * w[b] > v[b] * w[a]; });
    long long value = 0;
    for (int i : order) {
        if (w[i] <= cap) {
            cap -= w[i];
            value += v[i];
        }
    }
    return value;
}

void testKnapsackSlideExample() {
    std::cout << "knapsack slide example: capacity 4, A(3,9) B(2,5) C(2,5)\n";
    const std::vector<int> w = {3, 2, 2};
    const std::vector<long long> v = {9, 5, 5};
    const KnapsackResult r = knapsack01(w, v, 4);
    CHECK(r.value == 10);
    CHECK(r.weight == 4);
    CHECK(r.chosen == std::vector<int>({1, 2}));
    CHECK(greedyByRatio(w, v, 4) == 9);  // greedy takes A and is stuck
}

void testKnapsackEdgeCases() {
    std::cout << "knapsack edge cases\n";
    const std::vector<int> w = {5, 7};
    const std::vector<long long> v = {10, 20};

    KnapsackResult r = knapsack01(w, v, 0);
    CHECK(r.value == 0 && r.chosen.empty());

    r = knapsack01(w, v, 6);  // only the first item fits
    CHECK(r.value == 10 && r.chosen == std::vector<int>({0}));

    r = knapsack01(w, v, 100);  // everything fits
    CHECK(r.value == 30 && r.weight == 12);

    r = knapsack01(std::vector<int>(), std::vector<long long>(), 10);
    CHECK(r.value == 0 && r.chosen.empty());
}

// Compares the DP with trying every subset on 300 small random instances.
void testKnapsackAgainstBruteForce() {
    std::cout << "knapsack vs brute force on random instances\n";
    std::srand(42);
    int mismatches = 0;
    for (int trial = 0; trial < 300; ++trial) {
        const int n = 1 + std::rand() % 12;
        const int cap = std::rand() % 40;
        std::vector<int> w(n);
        std::vector<long long> v(n);
        for (int i = 0; i < n; ++i) {
            w[i] = 1 + std::rand() % 15;
            v[i] = 1 + std::rand() % 50;
        }
        long long best = 0;
        for (int mask = 0; mask < (1 << n); ++mask) {
            long long wt = 0, val = 0;
            for (int i = 0; i < n; ++i)
                if (mask & (1 << i)) {
                    wt += w[i];
                    val += v[i];
                }
            if (wt <= cap) best = std::max(best, val);
        }
        const KnapsackResult r = knapsack01(w, v, cap);
        long long wt = 0, val = 0;
        for (int i : r.chosen) {
            wt += w[i];
            val += v[i];
        }
        if (r.value != best || val != r.value || wt != r.weight || wt > cap) ++mismatches;
    }
    CHECK(mismatches == 0);
}

void testCrateSplitting(const std::string& root) {
    std::cout << "crate splitting: P11 -> N2 28 kg becomes 10 + 10 + 8\n";
    const Pipeline p = run(loadDataset(root + "/data/demo"), kParams);
    std::vector<int> sizes;
    for (const Crate& c : p.delivery.crates)
        if (p.data.packages[c.lot].id == "P11" && p.data.ngos[c.ngo].id == "N2") sizes.push_back(c.kg);
    CHECK(sizes == std::vector<int>({10, 10, 8}));

    // Every allocation is split exactly: crate kg per (lot, NGO) adds up to the allocation.
    std::map<std::pair<int, int>, long long> fromCrates, fromAllocations;
    for (const Crate& c : p.delivery.crates) fromCrates[std::make_pair(c.lot, c.ngo)] += c.kg;
    for (const Allocation& a : p.plan.allocations) fromAllocations[std::make_pair(a.lot, a.ngo)] += a.kg;
    CHECK(fromCrates == fromAllocations);
    for (const Crate& c : p.delivery.crates) CHECK(c.kg >= 1 && c.kg <= kParams.crate_kg);
}

void testD1ScooterTrips(const std::string& root) {
    std::cout << "D1 scooter (25 kg): 31 kg to N1 needs two trips\n";
    const Pipeline p = run(loadDataset(root + "/data/demo"), kParams);
    const std::vector<const Trip*> trips = tripsOf(p, "D1");
    CHECK(trips.size() == 2);
    if (trips.size() != 2) return;

    // Crates: P2#1 9, P1#1 10, P1#2 2, P3#1 10 (all tier 1). Best fill of 25 kg is 10 + 2 + 10 = 22.
    CHECK(tripCrates(p, *trips[0]) == std::vector<std::string>({"P1#1", "P1#2", "P3#1"}));
    CHECK(trips[0]->loadKg == 22);
    CHECK(near(trips[0]->departHours, 0.0));
    CHECK(near(trips[0]->servedByHours, 0.62));  // 3 km / 25 km/h + 0.5 h
    CHECK(near(trips[0]->returnHours, 0.74));
    CHECK(trips[0]->costPaise == 3600);         // 2 * 3 km * Rs 6

    CHECK(tripCrates(p, *trips[1]) == std::vector<std::string>({"P2#1"}));
    CHECK(near(trips[1]->departHours, 0.74));
    CHECK(near(trips[1]->servedByHours, 1.36));
}

void testD3KhichdiFirst(const std::string& root) {
    std::cout << "D3: Earliest Deadline First sends the first trip to N4 with the khichdi\n";
    const Pipeline p = run(loadDataset(root + "/data/demo"), kParams);
    const std::vector<const Trip*> trips = tripsOf(p, "D3");
    CHECK(!trips.empty());
    if (trips.empty()) return;
    CHECK(p.data.ngos[trips[0]->ngo].id == "N4");
    const std::vector<std::string> first = tripCrates(p, *trips[0]);
    CHECK(std::find(first.begin(), first.end(), "P10#1") != first.end());
    CHECK(std::find(first.begin(), first.end(), "P10#2") != first.end());
}

// Toy with a 4 kg van and 4 kg crates: TP1's 8 kg to TN2 needs two trips, and the
// second trip (leaves at 0.633 h, served at 1.2 h) is after TP1's 1.0 h expiry.
void testCrateExpiresBeforeLaterTrip(const std::string& root) {
    std::cout << "a crate that cannot make a later trip is reported as expired\n";
    Dataset data = loadDataset(root + "/data/toy");
    for (Vehicle& v : data.vehicles)
        if (v.id == "TV1") v.capacity_kg = 4;
    Params params = kParams;
    params.crate_kg = 4;

    const Pipeline p = run(data, params);
    CHECK(tripsOf(p, "TD1").size() == 1);
    CHECK(p.delivery.undelivered.size() == 1);
    if (p.delivery.undelivered.size() == 1) {
        CHECK(p.delivery.undelivered[0].reason == "expired");
        CHECK(crateLabel(p.data, p.delivery.crates[p.delivery.undelivered[0].crate]) == "TP1#2");
    }
    CHECK(p.delivery.deliveredKg == 12);
    CHECK(p.delivery.undeliveredKg == 4);
}

void testShiftAndCapacityLimits(const std::string& root) {
    std::cout << "shift end and too-heavy crates\n";
    Params shortShift = kParams;
    shortShift.shift_hours = 0.5;  // nothing can be served within half an hour (handling alone is 0.5 h)
    const Pipeline a = run(loadDataset(root + "/data/toy"), shortShift);
    CHECK(a.delivery.trips.empty());
    CHECK(a.delivery.undelivered.size() == 2);
    for (const Undelivered& u : a.delivery.undelivered) CHECK(u.reason == "shift over");

    Dataset data = loadDataset(root + "/data/toy");
    for (Vehicle& v : data.vehicles) v.capacity_kg = 5;  // crates are 8 kg
    const Pipeline b = run(data, kParams);
    CHECK(b.delivery.trips.empty());
    for (const Undelivered& u : b.delivery.undelivered) CHECK(u.reason == "too heavy");
    CHECK(b.delivery.undeliveredKg == 16);
}

// Rules every trip plan must follow.
void testDemoInvariants(const std::string& root) {
    std::cout << "demo trip invariants\n";
    const Pipeline p = run(loadDataset(root + "/data/demo"), kParams);
    const Dataset& d = p.data;

    std::vector<int> used(p.delivery.crates.size(), 0);
    std::map<int, double> lastReturn;
    long long delivered = 0, cost = 0;
    for (const Trip& t : p.delivery.trips) {
        CHECK(t.loadKg <= t.capacityKg);
        CHECK(t.servedByHours <= kParams.shift_hours + 1e-9);
        CHECK(t.departHours + 1e-9 >= (lastReturn.count(t.vehicle) ? lastReturn[t.vehicle] : 0.0));
        lastReturn[t.vehicle] = t.returnHours;

        int load = 0;
        for (int c : t.crates) {
            const Crate& cr = p.delivery.crates[c];
            ++used[c];
            load += cr.kg;
            CHECK(cr.ngo == t.ngo);                                                   // one NGO per trip
            CHECK(d.packages[cr.lot].donor_id == d.donors[t.donor].id);               // own donor's food
            CHECK(t.servedByHours <= d.packages[cr.lot].expiry_hours + 1e-9);         // arrives in time
        }
        CHECK(load == t.loadKg);
        delivered += t.loadKg;
        cost += t.costPaise;
    }
    for (const Undelivered& u : p.delivery.undelivered) ++used[u.crate];
    for (int n : used) CHECK(n == 1);  // every crate is either on exactly one trip or undelivered

    CHECK(delivered == p.delivery.deliveredKg);
    CHECK(cost == p.delivery.costPaise);
    CHECK(p.delivery.deliveredKg + p.delivery.undeliveredKg == p.plan.allocatedKg);
}

}  // namespace

int main(int argc, char** argv) {
    const std::string root = argc >= 2 ? argv[1] : ".";
    try {
        testKnapsackSlideExample();
        testKnapsackEdgeCases();
        testKnapsackAgainstBruteForce();
        testCrateSplitting(root);
        testD1ScooterTrips(root);
        testD3KhichdiFirst(root);
        testCrateExpiresBeforeLaterTrip(root);
        testShiftAndCapacityLimits(root);
        testDemoInvariants(root);
    } catch (const std::exception& e) {
        std::cerr << "error: " << e.what() << "\n";
        return 1;
    }

    std::cout << "\n" << (checks - failures) << "/" << checks << " checks passed\n";
    return failures == 0 ? 0 : 1;
}
