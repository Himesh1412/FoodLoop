// Tests for multi-stop routing (enhancements 3, 5, 6, 7, 8) and joint allocation-routing feedback (9).
// Hand-solved cases TR.1-TR.7, then randomised property checks TR.8 that re-verify every route
// with an independent simulation.

#include <cmath>
#include <iostream>
#include <map>
#include <random>
#include <string>

#include "allocation.hpp"
#include "data.hpp"
#include "dijkstra.hpp"
#include "feasibility.hpp"
#include "graph.hpp"
#include "metrics.hpp"
#include "routing.hpp"

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

Params baseParams() {
    Params p = {0, {6, 24}, 10, {3, 2, 1}, 100, 20, 1};
    p.vehicle_fixed_cost_inr = 500;
    p.unmet_penalty_inr = 1000;
    return p;
}

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
    Builder& vehicle(const std::string& id, const std::string& depotId, int cap, double speed = 10, double cost = 1) {
        Vehicle v{id, "", "van", cap, speed, cost};
        v.depot_id = depotId;
        d.vehicles.push_back(v);
        return *this;
    }
    Builder& donor(const std::string& id, const std::string& at, double service = 0) {
        Donor x{id, id, "pickup", at};
        x.service_hours = service;
        d.donors.push_back(x);
        return *this;
    }
    Builder& lot(const std::string& id, const std::string& donorId, int kg, double expiry = 100, double ready = 0,
                 double close = -1) {
        Package p{id, donorId, id, "VEG", kg, kg, expiry};
        p.ready_hours = ready;
        p.pickup_close_hours = close;
        d.packages.push_back(p);
        return *this;
    }
    Builder& ngo(const std::string& id, const std::string& at, int veg, double open = 0, double close = -1,
                 double service = 0) {
        Ngo n;
        n.id = n.name = id;
        n.type = "delivery";
        n.node_id = at;
        n.demand_kg["VEG"] = veg;
        n.demand_kg["NON_VEG"] = 0;
        n.demand_kg["VEGAN"] = 0;
        n.open_hours = open;
        n.close_hours = close;
        n.service_hours = service;
        d.ngos.push_back(n);
        return *this;
    }
};

struct Run {
    Dataset data;
    Graph g;
    Feasibility feas;
    AllocationPlan plan, joint;
    RoutePlan single, routed;
};

Run run(const Dataset& data, const Params& params) {
    validateDataset(data);
    Run r;
    r.data = data;
    r.g = buildRoadGraph(r.data);
    r.feas = computeFeasibility(r.data, computeDistanceMatrix(r.data, r.g), params);
    r.plan = allocateByTiers(r.data, r.feas, params);
    r.single = routeAllocation(r.data, r.g, r.plan, params);
    r.routed = planJointly(r.data, r.g, r.feas, params, r.joint);
    return r;
}

int hired(const RoutePlan& rp) {
    int n = 0;
    for (const Route& route : rp.routes) n += route.stops.empty() ? 0 : 1;
    return n;
}

// Independent re-check of a routed plan: windows, expiry, capacity, shift, pickup before delivery,
// conservation and the objective formula.
void verify(const Dataset& data, const Graph& g, const Params& params, const RoutePlan& rp) {
    std::map<std::string, int> donorIdx, depotIdx;
    for (size_t i = 0; i < data.donors.size(); ++i) donorIdx[data.donors[i].id] = static_cast<int>(i);
    for (size_t i = 0; i < data.depots.size(); ++i) depotIdx[data.depots[i].id] = static_cast<int>(i);
    std::map<std::string, ShortestPaths> runs;
    const auto km = [&](const std::string& a, const std::string& b) {
        if (!runs.count(a)) runs[a] = dijkstra(g, g.indexOf(a));
        return runs[a].dist[g.indexOf(b)];
    };
    std::vector<long long> fromLot(data.packages.size(), 0);
    std::map<std::pair<int, std::string>, long long> toNgo;
    long long delivered = 0, cost = 0;
    bool ok = true;
    for (const Route& route : rp.routes) {
        const Vehicle& veh = data.vehicles[route.vehicle];
        const std::string home = data.depots[depotIdx.at(veh.depot_id)].node_id;
        std::string at = home;
        double clock = 0, dist = 0;
        int load = 0;
        std::map<int, int> carried;  // request -> kg on board
        for (const RouteStop& st : route.stops) {
            const Request& q = rp.requests[st.request];
            const Package& lot = data.packages[q.lot];
            const Ngo& ngo = data.ngos[q.ngo];
            const std::string to = st.pickup ? data.donors[donorIdx.at(lot.donor_id)].node_id : ngo.node_id;
            dist += km(at, to);
            const double arrive = clock + km(at, to) / veh.speed_kmph;
            ok = ok && near(arrive, st.arriveHours);
            if (st.pickup) {
                ok = ok && st.startHours >= lot.ready_hours - 1e-9 && st.startHours <= lot.pickupClose() + 1e-9;
                load += st.kg;
                carried[st.request] += st.kg;
                clock = std::max(arrive, lot.ready_hours) + data.donors[donorIdx.at(lot.donor_id)].pickupService(params.handling_hours);
            } else {
                ok = ok && st.startHours >= ngo.open_hours - 1e-9;
                ok = ok && (!ngo.hasClose() || st.startHours <= ngo.close_hours + 1e-9);
                ok = ok && st.departHours <= lot.expiry_hours + 1e-9;
                ok = ok && carried[st.request] >= st.kg;  // picked up earlier on this vehicle
                carried[st.request] -= st.kg;
                load -= st.kg;
                delivered += st.kg;
                fromLot[q.lot] += st.kg;
                toNgo[std::make_pair(q.ngo, lot.category)] += st.kg;
                clock = std::max(arrive, ngo.open_hours) + ngo.deliveryService(params.handling_hours);
            }
            ok = ok && near(clock, st.departHours) && load == st.loadAfterKg && load >= 0 && load <= veh.capacity_kg;
            at = to;
        }
        if (!route.stops.empty()) {
            dist += km(at, home);
            clock += km(at, home) / veh.speed_kmph;
            ok = ok && clock <= params.shift_hours + 1e-9 && load == 0;
        }
        ok = ok && near(dist, route.km);
        cost += route.costPaise;
    }
    CHECK(ok);
    bool conserved = true;
    for (size_t k = 0; k < data.packages.size(); ++k) conserved = conserved && fromLot[k] <= data.packages[k].weight_kg;
    for (const auto& kv : toNgo) conserved = conserved && kv.second <= data.ngos[kv.first.first].demandFor(kv.first.second);
    CHECK(conserved);
    CHECK(delivered == rp.deliveredKg && cost == rp.costPaise && hired(rp) == rp.vehiclesUsed);
    long long supply = 0, demand = 0;
    for (const Package& p : data.packages) supply += p.weight_kg;
    for (const Ngo& n : data.ngos) demand += n.demandFor("VEG") + n.demandFor("NON_VEG") + n.demandFor("VEGAN");
    const long long unmet = (supply - delivered) + (demand - delivered);
    CHECK(rp.unmetUnits == unmet);
    CHECK(near(rp.objective, rp.costPaise / 100.0 + rp.vehiclesUsed * params.vehicle_fixed_cost_inr +
                                 params.unmet_penalty_inr * unmet));
}

// Depot A, donor B (5 km), NGOs C and E.
Builder star() {
    Builder b;
    b.node("A").node("B").node("C").node("E").road("A", "B", 5).road("B", "C", 1).road("B", "E", 3);
    b.depot("H", "A").donor("D", "B");
    return b;
}

void testMultiStop() {
    std::cout << "TR.1 one vehicle serves two NGOs from one pickup site on a single route\n";
    Builder b = star();
    b.vehicle("V1", "H", 50).lot("L", "D", 30).ngo("N1", "C", 20).ngo("N2", "E", 10);
    const Params p = baseParams();
    const Run r = run(b.d, p);
    CHECK(r.routed.deliveredKg == 30 && r.routed.vehiclesUsed == 1);
    CHECK(r.routed.routes[0].stops.size() == 4);  // two pickups (split pickup) and two deliveries
    int maxLoad = 0;
    for (const RouteStop& st : r.routed.routes[0].stops) maxLoad = std::max(maxLoad, st.loadAfterKg);
    CHECK(maxLoad == r.routed.routes[0].maxLoadKg && maxLoad <= 50);
    verify(r.data, r.g, p, r.routed);
}

void testCapacitySplit() {
    std::cout << "TR.2 a lot larger than a vehicle is split across vehicles (split pickup)\n";
    Builder b = star();
    b.vehicle("V1", "H", 30).vehicle("V2", "H", 30).lot("L", "D", 60, 100, 0, 0.6).ngo("N1", "C", 60);
    const Params p = baseParams();
    const Run r = run(b.d, p);  // pickup closes at 0.6 h: both loads must be collected on the first visit
    CHECK(r.routed.deliveredKg == 60 && r.routed.vehiclesUsed == 2);
    verify(r.data, r.g, p, r.routed);
}

void testWindowsAndService() {
    std::cout << "TR.3 NGO opening window and service times\n";
    Builder b = star();
    b.d.donors.clear();
    b.donor("D", "B", 0.2).vehicle("V1", "H", 50).lot("L", "D", 20).ngo("N1", "C", 20, 3.0, 5.0, 0.1);
    const Params p = baseParams();
    const Run r = run(b.d, p);
    CHECK(r.routed.deliveredKg == 20);
    const std::vector<RouteStop>& s = r.routed.routes[0].stops;
    CHECK(s.size() == 2 && near(s[0].arriveHours, 0.5) && near(s[0].departHours, 0.7));
    CHECK(near(s[1].arriveHours, 0.8) && near(s[1].startHours, 3.0) && near(s[1].departHours, 3.1));
    CHECK(near(r.routed.routes[0].endHours, 3.1 + 0.6));  // C -> B -> A = 6 km at 10 km/h
    verify(r.data, r.g, p, r.routed);
}

void testHiringCost() {
    std::cout << "TR.4 hiring cost: one vehicle is reused instead of hiring a second\n";
    Builder b = star();
    b.vehicle("V1", "H", 100).vehicle("V2", "H", 100).lot("L", "D", 40).ngo("N1", "C", 20).ngo("N2", "E", 20);
    const Params p = baseParams();
    const Run r = run(b.d, p);
    CHECK(r.routed.deliveredKg == 40 && r.routed.vehiclesUsed == 1);
    verify(r.data, r.g, p, r.routed);
}

void testObjective() {
    std::cout << "TR.5 base-paper objective = routing + hiring + penalty x (unmet pickup + unmet delivery)\n";
    Builder b = star();
    b.vehicle("V1", "H", 50).lot("L", "D", 30).ngo("N1", "C", 20);  // 10 kg can never be placed
    const Params p = baseParams();
    const Run r = run(b.d, p);
    CHECK(r.routed.deliveredKg == 20 && r.routed.unmetUnits == 10);
    CHECK(r.routed.km > 0);
    CHECK(near(r.routed.objective, r.routed.costPaise / 100.0 + 500 + 10 * 1000));
    const Metrics m = computeMetrics(r.joint, r.routed, p, 0);
    CHECK(near(m.objective, r.routed.objective) && m.vehiclesUsed == 1 && m.unmetUnits == 10);
    verify(r.data, r.g, p, r.routed);
}

void testFeedback() {
    std::cout << "TR.6 feedback re-allocates a pair that the vehicle cannot reach in time\n";
    // MCMF prefers N1 (1 km) but N1 closes at 0.3 h; the vehicle reaches the donor only at 0.5 h.
    Builder b = star();
    b.vehicle("V1", "H", 50).lot("L", "D", 20).ngo("N1", "C", 20, 0, 0.3).ngo("N2", "E", 20);
    const Params p = baseParams();
    const Run r = run(b.d, p);
    CHECK(r.plan.allocations.size() == 1 && r.data.ngos[r.plan.allocations[0].ngo].id == "N1");
    CHECK(r.single.deliveredKg == 0);   // without feedback the food is stranded
    CHECK(r.routed.deliveredKg == 20);  // with feedback it goes to N2
    CHECK(r.routed.objective < r.single.objective);
    verify(r.data, r.g, p, r.routed);
    verify(r.data, r.g, p, r.single);
}

void testPickupBeforeDelivery() {
    std::cout << "TR.7 delivery-only first stops are impossible (vehicles start empty)\n";
    Builder b = star();
    b.vehicle("V1", "H", 50).lot("L", "D", 20, 100, 2.0).ngo("N1", "C", 20, 0, 1.0);
    const Params p = baseParams();
    const Run r = run(b.d, p);  // food ready only at 2 h, NGO closes at 1 h
    CHECK(r.routed.deliveredKg == 0 && r.routed.vehiclesUsed == 0);
    CHECK(near(r.routed.objective, 40 * 1000));
}

void testRandom() {
    std::cout << "TR.8 200 random instances: every route re-verified independently\n";
    std::mt19937 rng(11);
    std::uniform_real_distribution<double> u01(0, 1);
    int delivering = 0, feedbackHelps = 0, notWorse = 0;
    for (int inst = 0; inst < 200; ++inst) {
        Builder b;
        const int n = 10;
        for (int i = 0; i < n; ++i) b.node("J" + std::to_string(i));
        for (int i = 1; i < n; ++i) b.road("J" + std::to_string(i), "J" + std::to_string(rng() % i), 1 + 4 * u01(rng));
        for (int e = 0; e < 5; ++e) b.road("J" + std::to_string(rng() % n), "J" + std::to_string(rng() % n), 1 + 6 * u01(rng));
        b.depot("H0", "J0").depot("H1", "J" + std::to_string(1 + rng() % (n - 1)));
        for (int k = 0; k < 2 + inst % 3; ++k)
            b.vehicle("V" + std::to_string(k), k % 2 ? "H1" : "H0", 20 + rng() % 60, 10 + 20 * u01(rng), 1 + rng() % 3);
        for (int k = 0; k < 4; ++k) b.donor("D" + std::to_string(k), "J" + std::to_string(rng() % n), 0.1 * (rng() % 3));
        for (int k = 0; k < 6; ++k) {
            const double ready = 2 * u01(rng);
            b.lot("P" + std::to_string(k), "D" + std::to_string(k % 4), 5 + rng() % 40, ready + 2 + 6 * u01(rng), ready,
                  u01(rng) < 0.5 ? -1 : ready + 1 + 2 * u01(rng));
        }
        for (int k = 0; k < 5; ++k) {
            const double open = u01(rng) < 0.5 ? 0 : 2 * u01(rng);
            b.ngo("N" + std::to_string(k), "J" + std::to_string(rng() % n), 5 + rng() % 40, open,
                  u01(rng) < 0.5 ? -1 : open + 1 + 4 * u01(rng), 0.1 * (rng() % 3));
        }
        Params p = baseParams();
        p.shift_hours = 6 + 6 * u01(rng);
        const Run r = run(b.d, p);
        verify(r.data, r.g, p, r.routed);
        verify(r.data, r.g, p, r.single);
        if (r.routed.deliveredKg > 0) ++delivering;
        if (r.routed.objective < r.single.objective - 1e-6) ++feedbackHelps;
        if (r.routed.objective <= r.single.objective + 1e-6) ++notWorse;
    }
    CHECK(delivering > 150);
    CHECK(notWorse == 200);  // the joint planner keeps round 1 as a candidate, so it is never worse
    CHECK(feedbackHelps > 0);
    std::cout << "    feedback improved the objective on " << feedbackHelps << " of 200 instances\n";
}

}  // namespace

int main() {
    testMultiStop();
    testCapacitySplit();
    testWindowsAndService();
    testHiringCost();
    testObjective();
    testFeedback();
    testPickupBeforeDelivery();
    testRandom();
    std::cout << "\n" << (checks - failures) << "/" << checks << " checks passed\n";
    return failures == 0 ? 0 : 1;
}
