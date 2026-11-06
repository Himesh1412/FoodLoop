// Tests for the shared-fleet dispatch stage (enhancements 1, 2, 4, 5, 6, 7, 8).
// Hand-solved cases first (TF.1-TF.12), then randomised property checks (TF.13).

#include <cmath>
#include <iostream>
#include <map>
#include <random>
#include <set>
#include <stdexcept>
#include <string>

#include "allocation.hpp"
#include "data.hpp"
#include "dijkstra.hpp"
#include "dispatch.hpp"
#include "feasibility.hpp"
#include "graph.hpp"
#include "metrics.hpp"

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

struct Builder {
    Dataset d;
    Builder& node(const std::string& id) {
        d.nodes.push_back(Node{id, 0, 0, id});
        return *this;
    }
    Builder& road(const std::string& u, const std::string& v, double km) {
        d.roads.push_back(Road{u, v, km});
        return *this;
    }
    Builder& depot(const std::string& id, const std::string& at) {
        d.depots.push_back(Depot{id, id, "depot", at});
        return *this;
    }
    Builder& vehicle(const std::string& id, const std::string& depotId, int cap, double speed, double costPerKm) {
        Vehicle v{id, "", "van", cap, speed, costPerKm};
        v.depot_id = depotId;
        d.vehicles.push_back(v);
        return *this;
    }
    Builder& donor(const std::string& id, const std::string& at) {
        d.donors.push_back(Donor{id, id, "restaurant", at});
        return *this;
    }
    Builder& lot(const std::string& id, const std::string& donorId, int kg, double expiry, double ready = 0,
                 double close = -1) {
        Package p{id, donorId, id, "VEG", kg * 2, kg, expiry};
        p.ready_hours = ready;
        p.pickup_close_hours = close;
        d.packages.push_back(p);
        return *this;
    }
    Builder& ngo(const std::string& id, const std::string& at, int veg, double open = 0, double close = -1) {
        Ngo n;
        n.id = n.name = id;
        n.type = "shelter";
        n.node_id = at;
        n.demand_kg["VEG"] = veg;
        n.demand_kg["NON_VEG"] = 0;
        n.demand_kg["VEGAN"] = 0;
        n.open_hours = open;
        n.close_hours = close;
        d.ngos.push_back(n);
        return *this;
    }
};

struct Run {
    Dataset data;
    Graph g;
    Feasibility feas;
    AllocationPlan plan;
    FleetPlan fleet;
};

Run run(const Dataset& data, const Params& params = kParams, DispatchPolicy policy = DispatchPolicy::COST_AWARE) {
    validateDataset(data);
    Run r;
    r.data = data;
    r.g = buildRoadGraph(r.data);
    const DistanceMatrix m = computeDistanceMatrix(r.data, r.g);
    r.feas = computeFeasibility(r.data, m, params);
    r.plan = allocateByTiers(r.data, r.feas, params);
    r.fleet = dispatchFleet(r.data, r.g, r.plan, params, policy);
    return r;
}

// Line A --10-- B --10-- C: depot at A, donor at B, NGO at C.
Builder line() {
    Builder b;
    b.node("A").node("B").node("C").road("A", "B", 10).road("B", "C", 10).depot("H", "A").donor("D", "B");
    return b;
}

std::string reasonOf(const Run& r) { return r.fleet.undelivered.empty() ? "" : r.fleet.undelivered[0].reason; }

void testBasicTimeline() {
    std::cout << "TF.1 depot -> donor -> NGO -> depot timeline, load and cost\n";
    Builder b = line();
    b.vehicle("V1", "H", 50, 10, 10).lot("P1", "D", 20, 10).ngo("N", "C", 20);
    const Run r = run(b.d);
    CHECK(r.fleet.trips.size() == 1);
    const FleetTrip& t = r.fleet.trips[0];
    CHECK(t.fromPlace == "H");
    CHECK(near(t.pickup.arriveHours, 1.0) && near(t.pickup.serviceStartHours, 1.0) && near(t.pickup.departHours, 1.25));
    CHECK(near(t.delivery.arriveHours, 2.25) && near(t.delivery.serviceStartHours, 2.25));
    CHECK(near(t.delivery.departHours, 2.5));
    CHECK(t.pickup.loadAfterKg == 20 && t.delivery.loadAfterKg == 0);  // enhancement 6
    CHECK(near(t.emptyKm, 10) && near(t.loadedKm, 10));
    CHECK(t.costPaise == 20000);                     // 20 km x Rs 10
    CHECK(r.fleet.costPaise == 40000);               // + 20 km back to the depot
    CHECK(near(r.fleet.km, 40));
    CHECK(near(r.fleet.vehicles[0].endHours, 4.5));
    CHECK(r.fleet.deliveredKg == 20 && r.fleet.undeliveredKg == 0);
    const Metrics m = computeMetrics(r.plan, r.fleet, kParams, 0);
    CHECK(m.deliveredKg == 20 && near(m.expiryFeasiblePct, 100) && m.tripCostPaise == 40000);
    CHECK(m.vehiclesUsed == 1 && near(m.km, 40) && m.trips == 1);
}

void testReadyTimeWait() {
    std::cout << "TF.2 food-ready time: vehicle waits at the donor\n";
    Builder b = line();
    b.vehicle("V1", "H", 50, 10, 10).lot("P1", "D", 20, 10, 3.0).ngo("N", "C", 20);
    const Run r = run(b.d);
    CHECK(r.fleet.trips.size() == 1);
    const FleetTrip& t = r.fleet.trips[0];
    CHECK(near(t.pickup.arriveHours, 1.0) && near(t.pickup.serviceStartHours, 3.0));
    CHECK(near(t.waitHours, 2.0));
    CHECK(near(t.delivery.departHours, 4.5));
}

void testPickupWindowClosed() {
    std::cout << "TF.3 pickup window closes before the vehicle arrives\n";
    Builder b = line();
    b.vehicle("V1", "H", 50, 10, 10).lot("P1", "D", 20, 10, 0, 0.5).ngo("N", "C", 20);
    const Run r = run(b.d);
    CHECK(r.plan.allocatedKg == 20);  // allocation is optimistic (vehicle could be pre-positioned)
    CHECK(r.fleet.trips.empty());
    CHECK(r.fleet.undeliveredKg == 20);
    CHECK(reasonOf(r) == "pickup window closed");
}

void testNgoWindow() {
    std::cout << "TF.4 NGO opening window: wait to open, or report closed\n";
    {
        Builder b = line();
        b.vehicle("V1", "H", 50, 10, 10).lot("P1", "D", 20, 10).ngo("N", "C", 20, 3.0);
        const Run r = run(b.d);
        CHECK(r.fleet.trips.size() == 1);
        CHECK(near(r.fleet.trips[0].delivery.arriveHours, 2.25));
        CHECK(near(r.fleet.trips[0].delivery.serviceStartHours, 3.0));
        CHECK(near(r.fleet.trips[0].waitHours, 0.75));
    }
    {
        Builder b = line();
        b.vehicle("V1", "H", 50, 10, 10).lot("P1", "D", 20, 10).ngo("N", "C", 20, 0, 2.0);
        const Run r = run(b.d);
        CHECK(r.plan.allocatedKg == 20);
        CHECK(r.fleet.trips.empty());
        CHECK(reasonOf(r) == "NGO closed");
    }
}

void testFeasibilityWindows() {
    std::cout << "TF.5 feasibility uses ready time, NGO close and the fastest fleet vehicle\n";
    {   // ready 9.5 + 0.25 + 1.0 (fastest 10 km/h) + 0.25 = 11 > expiry 10 -> too late
        Builder b = line();
        b.vehicle("V1", "H", 50, 10, 10).lot("P1", "D", 20, 10, 9.5).ngo("N", "C", 20);
        const Run r = run(b.d);
        CHECK(r.feas.removedExpiry == 1 && r.feas.keptPairs == 0 && r.plan.allocatedKg == 0);
    }
    {   // NGO closes at 1.0, earliest service start 1.25 -> outside window
        Builder b = line();
        b.vehicle("V1", "H", 50, 10, 10).lot("P1", "D", 20, 10).ngo("N", "C", 20, 0, 1.0);
        const Run r = run(b.d);
        CHECK(r.feas.removedWindow == 1 && r.feas.keptPairs == 0);
    }
    {   // a fast second vehicle makes the pair feasible: 0.25 + 10/40 + 0.25 = 0.75 <= 0.8
        Builder b = line();
        b.vehicle("V1", "H", 50, 10, 10).vehicle("V2", "H", 50, 40, 10).lot("P1", "D", 20, 0.8).ngo("N", "C", 20);
        const Run r = run(b.d);
        CHECK(r.feas.keptPairs == 1);
        CHECK(r.fleet.trips.empty() || r.fleet.trips[0].vehicle == 1);
    }
}

void testCostAwareVsNearest() {
    std::cout << "TF.6 cost-aware dispatch vs nearest-vehicle dispatch (ablation)\n";
    // Depot X is 2 km from the donor (expensive vehicle), depot Y is 4 km away (cheap vehicle).
    Builder b;
    b.node("X").node("Y").node("B").node("C").road("X", "B", 2).road("Y", "B", 4).road("B", "C", 10);
    b.depot("HX", "X").depot("HY", "Y").donor("D", "B");
    b.vehicle("VX", "HX", 50, 20, 50).vehicle("VY", "HY", 50, 20, 5);
    b.lot("P1", "D", 20, 10).ngo("N", "C", 20);
    const Run cost = run(b.d);
    const Run nearest = run(b.d, kParams, DispatchPolicy::NEAREST_VEHICLE);
    CHECK(cost.fleet.trips.size() == 1 && nearest.fleet.trips.size() == 1);
    CHECK(cost.fleet.trips[0].vehicle == 1);     // cheap VY: (4 + 10) x 5 = Rs 70
    CHECK(nearest.fleet.trips[0].vehicle == 0);  // first to arrive: VX (2 km)
    CHECK(cost.fleet.trips[0].costPaise == 7000);
    CHECK(nearest.fleet.trips[0].costPaise == 60000);
    CHECK(cost.fleet.costPaise < nearest.fleet.costPaise);
    CHECK(cost.fleet.deliveredKg == nearest.fleet.deliveredKg);
}

void testSplitAcrossVehicles() {
    std::cout << "TF.7 split service: one lot carried by two vehicles\n";
    // 60 kg, two 30 kg vehicles. One vehicle doing two jobs would deliver the second at 5.0 h > expiry 3.
    Builder b = line();
    b.vehicle("V1", "H", 30, 10, 10).vehicle("V2", "H", 30, 10, 10).lot("P1", "D", 60, 3).ngo("N", "C", 60);
    const Run r = run(b.d);
    CHECK(r.fleet.trips.size() == 2);
    CHECK(r.fleet.trips.size() == 2 && r.fleet.trips[0].vehicle != r.fleet.trips[1].vehicle);
    CHECK(r.fleet.deliveredKg == 60 && r.fleet.undeliveredKg == 0);
    for (const FleetTrip& t : r.fleet.trips) CHECK(t.loadKg == 30 && t.pickup.loadAfterKg == 30);
}

void testSplitAcrossNgos() {
    std::cout << "TF.8 split service: one lot delivered to two NGOs by one shared vehicle\n";
    Builder b = line();
    b.node("E").road("B", "E", 5).vehicle("V1", "H", 50, 10, 10).lot("P1", "D", 30, 10);
    b.ngo("N1", "C", 20).ngo("N2", "E", 10);
    const Run r = run(b.d);
    CHECK(r.fleet.deliveredKg == 30);
    std::set<int> ngos;
    for (const FleetTrip& t : r.fleet.trips) ngos.insert(t.ngo);
    CHECK(ngos.size() == 2);
    CHECK(r.fleet.trips.size() == 2 && r.fleet.trips[1].fromPlace != "H");  // second job starts at an NGO
}

void testEarliestDeadlineFirst() {
    std::cout << "TF.9 most urgent food is scheduled first\n";
    Builder b;
    b.node("A").node("B1").node("B2").node("C");
    b.road("A", "B1", 5).road("A", "B2", 5).road("B1", "C", 5).road("B2", "C", 5);
    b.depot("H", "A").donor("D1", "B1").donor("D2", "B2").vehicle("V1", "H", 50, 10, 10);
    b.lot("Slow", "D1", 10, 20).lot("Fast", "D2", 10, 2.5).ngo("N", "C", 20);
    const Run r = run(b.d);
    CHECK(r.fleet.trips.size() == 2);
    CHECK(r.fleet.trips.size() == 2 && r.data.donors[r.fleet.trips[0].donor].id == "D2");
    CHECK(r.fleet.deliveredKg == 20);
}

void testTooHeavyAndShift() {
    std::cout << "TF.10 too heavy and shift over\n";
    {
        Builder b = line();
        b.vehicle("V1", "H", 5, 10, 10).lot("P1", "D", 10, 10).ngo("N", "C", 10);  // crate 10 kg > 5 kg
        const Run r = run(b.d);
        CHECK(r.fleet.trips.empty() && reasonOf(r) == "too heavy");
    }
    {
        Params p = kParams;
        p.shift_hours = 3.0;  // delivery done at 2.5 h but back at the depot only at 4.5 h
        Builder b = line();
        b.vehicle("V1", "H", 50, 10, 10).lot("P1", "D", 20, 10).ngo("N", "C", 20);
        const Run r = run(b.d, p);
        CHECK(r.fleet.trips.empty() && reasonOf(r) == "shift over");
    }
}

void testValidation() {
    std::cout << "TF.11 validation of fleet data\n";
    {
        Builder b = line();
        b.vehicle("V1", "NOPE", 50, 10, 10).lot("P1", "D", 20, 10).ngo("N", "C", 20);
        bool threw = false;
        try { validateDataset(b.d); } catch (const std::runtime_error&) { threw = true; }
        CHECK(threw);
    }
    {
        Builder b = line();
        b.vehicle("V1", "H", 50, 10, 10).lot("P1", "D", 20, 10, 5, 4).ngo("N", "C", 20);  // closes before ready
        bool threw = false;
        try { validateDataset(b.d); } catch (const std::runtime_error&) { threw = true; }
        CHECK(threw);
    }
    {
        Builder b = line();
        b.vehicle("V1", "H", 50, 10, 10).lot("P1", "D", 20, 10).ngo("N", "C", 20, 5, 4);  // NGO closes before open
        bool threw = false;
        try { validateDataset(b.d); } catch (const std::runtime_error&) { threw = true; }
        CHECK(threw);
    }
}

void testDonorModeUnchanged() {
    std::cout << "TF.12 without depots the dataset stays in donor mode\n";
    Builder b = line();
    b.d.depots.clear();
    b.d.donors[0].id = "D";
    b.d.vehicles.push_back(Vehicle{"V1", "D", "van", 50, 10, 10});
    b.lot("P1", "D", 20, 10).ngo("N", "C", 20);
    validateDataset(b.d);
    CHECK(!b.d.fleetMode());
    bool threw = false;
    Graph g = buildRoadGraph(b.d);
    try { dispatchFleet(b.d, g, AllocationPlan(), kParams); } catch (const std::runtime_error&) { threw = true; }
    CHECK(threw);
}

// Every schedule must respect time, window, capacity and conservation rules.
void checkInvariants(const Run& r, const Params& params) {
    const FleetPlan& f = r.fleet;
    long long allocated = 0;
    for (const Allocation& a : r.plan.allocations) allocated += a.kg;
    CHECK(f.deliveredKg + f.undeliveredKg == allocated);

    std::map<int, int> seen;  // crate -> times used
    for (const FleetTrip& t : f.trips)
        for (int c : t.crates) ++seen[c];
    for (const Undelivered& u : f.undelivered) ++seen[u.crate];
    bool once = seen.size() == f.crates.size();
    for (const auto& kv : seen) once = once && kv.second == 1;
    CHECK(once);

    std::map<int, double> lastDepart;  // vehicle -> time it left its last delivery
    long long cost = 0;
    for (const FleetTrip& t : f.trips) {
        const Vehicle& v = r.data.vehicles[t.vehicle];
        const Ngo& ngo = r.data.ngos[t.ngo];
        int load = 0;
        bool ok = true;
        for (int c : t.crates) {
            const Crate& cr = f.crates[c];
            const Package& p = r.data.packages[cr.lot];
            load += cr.kg;
            ok = ok && cr.ngo == t.ngo && p.donor_id == r.data.donors[t.donor].id;
            ok = ok && t.pickup.serviceStartHours >= p.ready_hours - 1e-9;
            ok = ok && t.pickup.serviceStartHours <= p.pickupClose() + 1e-9;
            ok = ok && t.delivery.departHours <= p.expiry_hours + 1e-9;
        }
        CHECK(ok);
        CHECK(load == t.loadKg && load <= v.capacity_kg && t.pickup.loadAfterKg == load && t.delivery.loadAfterKg == 0);
        CHECK(t.pickup.arriveHours >= t.leaveHours - 1e-9);
        CHECK(t.pickup.serviceStartHours >= t.pickup.arriveHours - 1e-9);
        CHECK(near(t.pickup.departHours, t.pickup.serviceStartHours + params.handling_hours / 2));
        CHECK(t.delivery.arriveHours >= t.pickup.departHours - 1e-9);
        CHECK(t.delivery.serviceStartHours >= std::max(t.delivery.arriveHours, ngo.open_hours) - 1e-9);
        CHECK(!ngo.hasClose() || t.delivery.serviceStartHours <= ngo.close_hours + 1e-9);
        if (lastDepart.count(t.vehicle)) CHECK(t.leaveHours >= lastDepart[t.vehicle] - 1e-9);
        lastDepart[t.vehicle] = t.delivery.departHours;
        cost += t.costPaise;
    }
    long long dayCost = 0;
    for (const VehicleDay& d : f.vehicles) {
        CHECK(d.endHours <= params.shift_hours + 1e-9);
        dayCost += d.costPaise;
    }
    CHECK(dayCost == f.costPaise && cost <= f.costPaise);
}

void testRandomInvariants() {
    std::cout << "TF.13 300 random fleet instances x 4 rules: schedule invariants\n";
    std::mt19937 rng(2026);
    std::uniform_real_distribution<double> u01(0, 1);
    int delivering = 0;
    for (int inst = 0; inst < 300; ++inst) {
        Builder b;
        const int n = 6 + inst % 5;
        for (int i = 0; i < n; ++i) b.node("J" + std::to_string(i));
        for (int i = 1; i < n; ++i) b.road("J" + std::to_string(i), "J" + std::to_string(rng() % i), 1 + 6 * u01(rng));
        for (int e = 0; e < n / 2; ++e)
            b.road("J" + std::to_string(rng() % n), "J" + std::to_string(rng() % n), 1 + 8 * u01(rng));
        const int depots = 1 + inst % 2;
        for (int k = 0; k < depots; ++k) b.depot("H" + std::to_string(k), "J" + std::to_string(rng() % n));
        for (int k = 0; k < 1 + inst % 3; ++k)
            b.vehicle("V" + std::to_string(k), "H" + std::to_string(k % depots), 15 + rng() % 60, 15 + 15 * u01(rng),
                      5 + rng() % 10);
        for (int k = 0; k < 3; ++k) b.donor("D" + std::to_string(k), "J" + std::to_string(rng() % n));
        for (int k = 0; k < 6; ++k) {
            const double ready = u01(rng) < 0.5 ? 0 : 3 * u01(rng);
            const double expiry = ready + 0.5 + 8 * u01(rng);
            const double close = u01(rng) < 0.5 ? -1 : ready + 2 * u01(rng);
            b.lot("P" + std::to_string(k), "D" + std::to_string(k % 3), 5 + rng() % 30, expiry, ready, close);
        }
        for (int k = 0; k < 4; ++k) {
            const double open = u01(rng) < 0.5 ? 0 : 2 * u01(rng);
            const double close = u01(rng) < 0.5 ? -1 : open + 2 + 4 * u01(rng);
            b.ngo("N" + std::to_string(k), "J" + std::to_string(rng() % n), 10 + rng() % 40, open, close);
        }
        Params p = kParams;
        p.shift_hours = 6 + 4 * u01(rng);
        for (DispatchPolicy policy : {DispatchPolicy::COST_AWARE, DispatchPolicy::BALANCED, DispatchPolicy::NEAREST_VEHICLE,
                                      DispatchPolicy::THROUGHPUT}) {
            const Run r = run(b.d, p, policy);
            checkInvariants(r, p);
            if (r.fleet.deliveredKg > 0) ++delivering;
        }
    }
    CHECK(delivering > 600);  // most random instances deliver something, so the checks are not vacuous
}

void testBestOfPortfolio() {
    std::cout << "TF.14 proposed dispatcher is never worse than any single rule (200 random instances)\n";
    std::mt19937 rng(7);
    std::uniform_real_distribution<double> u01(0, 1);
    int strictlyBetterThanNearest = 0;
    for (int inst = 0; inst < 200; ++inst) {
        Builder b;
        for (int i = 0; i < 9; ++i) b.node("J" + std::to_string(i));
        for (int i = 1; i < 9; ++i) b.road("J" + std::to_string(i), "J" + std::to_string(rng() % i), 1 + 6 * u01(rng));
        b.depot("H0", "J0").depot("H1", "J8");
        for (int k = 0; k < 3; ++k)
            b.vehicle("V" + std::to_string(k), k % 2 ? "H1" : "H0", 20 + rng() % 60, 15 + 15 * u01(rng), 4 + rng() % 12);
        for (int k = 0; k < 3; ++k) b.donor("D" + std::to_string(k), "J" + std::to_string(1 + rng() % 7));
        for (int k = 0; k < 7; ++k) {
            const double ready = 2 * u01(rng);
            b.lot("P" + std::to_string(k), "D" + std::to_string(k % 3), 5 + rng() % 25, ready + 1 + 5 * u01(rng), ready,
                  ready + 1 + 2 * u01(rng));
        }
        for (int k = 0; k < 4; ++k) b.ngo("N" + std::to_string(k), "J" + std::to_string(1 + rng() % 7), 10 + rng() % 30);
        validateDataset(b.d);
        Graph g = buildRoadGraph(b.d);
        const DistanceMatrix m = computeDistanceMatrix(b.d, g);
        const AllocationPlan plan = allocateByTiers(b.d, computeFeasibility(b.d, m, kParams), kParams);
        const FleetPlan best = dispatchBest(b.d, g, plan, kParams);
        const FleetPlan cost = dispatchFleet(b.d, g, plan, kParams, DispatchPolicy::COST_AWARE);
        const FleetPlan nearest = dispatchFleet(b.d, g, plan, kParams, DispatchPolicy::NEAREST_VEHICLE);
        const FleetPlan rate = dispatchFleet(b.d, g, plan, kParams, DispatchPolicy::THROUGHPUT);
        CHECK(!betterFleetPlan(cost, best) && !betterFleetPlan(nearest, best) && !betterFleetPlan(rate, best));
        CHECK(best.deliveredKg >= nearest.deliveredKg && best.deliveredKg >= cost.deliveredKg);
        if (betterFleetPlan(best, nearest)) ++strictlyBetterThanNearest;
    }
    CHECK(strictlyBetterThanNearest > 0);
}

}  // namespace

int main() {
    testBasicTimeline();
    testReadyTimeWait();
    testPickupWindowClosed();
    testNgoWindow();
    testFeasibilityWindows();
    testCostAwareVsNearest();
    testSplitAcrossVehicles();
    testSplitAcrossNgos();
    testEarliestDeadlineFirst();
    testTooHeavyAndShift();
    testValidation();
    testDonorModeUnchanged();
    testRandomInvariants();
    testBestOfPortfolio();
    std::cout << "\n" << (checks - failures) << "/" << checks << " checks passed\n";
    return failures == 0 ? 0 : 1;
}
