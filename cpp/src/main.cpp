// FoodLoop command-line engine.
//
//   foodloop <dataset_dir> [--json] [--params config/params.csv] [--repeat N]
//
// Workflow (docs/problem_formulation.md section 2):
//   1-2  load and validate the datasets
//   3    Dijkstra donor -> NGO distances
//   4-5  feasibility: compatibility, reachability, expiry
//   6    Greedy expiry ranking and urgency tiers
//   7    category-wise Min Cost Max Flow, tier by tier
//   8    Knapsack vehicle loading (trips)
//   9-10 allocation results and metrics
//   11   --json output for the Streamlit UI
// The Greedy baseline allocator runs on the same feasibility data, followed by the same
// trip planner, so both methods are compared under identical constraints.
// --repeat N averages the allocation execution time over at least N runs (default 20)
// and at least 50 ms of measurement.

#include <chrono>
#include <cmath>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <sstream>
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

// Average time of each pipeline stage in ms (see averageMs), for the scalability analysis.
struct StageTimes {
    double dijkstra = 0;
    double feasibility = 0;
    double greedyRanking = 0;
    double mcmf = 0;
    double trips = 0;
    double greedyAllocation = 0;
};

struct Report {
    std::string dir;
    Dataset data;
    Params params;
    Graph graph;
    DistanceMatrix matrix;
    std::vector<Package> priority;
    Feasibility feas;

    AllocationPlan plan;  // proposed method: tiered MCMF
    DeliveryPlan delivery;
    Metrics metrics;

    AllocationPlan greedyPlan;  // baseline
    DeliveryPlan greedyDelivery;
    Metrics greedyMetrics;

    StageTimes stageMs;
};

// ET = T_end - T_start, averaged over at least `repeat` runs. Keeps repeating until 50 ms
// have passed, because the MinGW steady_clock only ticks about once per millisecond.
template <class F>
double averageMs(F run, int repeat) {
    const std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
    std::chrono::duration<double, std::milli> elapsed(0);
    int runs = 0;
    while (runs < repeat || elapsed.count() < 50.0) {
        run();
        ++runs;
        elapsed = std::chrono::steady_clock::now() - start;
    }
    return elapsed.count() / runs;
}

// Rounds to 3 decimals so 3.0 + 3.1 prints as 6.1, not 6.1000000000000005.
double round3(double x) { return std::floor(x * 1000.0 + 0.5) / 1000.0; }

std::string fixedText(double x, int precision) {
    std::ostringstream o;
    o << std::fixed << std::setprecision(precision) << x;
    return o.str();
}

std::string numText(long long x) {
    std::ostringstream o;
    o << x;
    return o.str();
}

std::string rupees(long long paise) { return "Rs " + fixedText(paise / 100.0, 2); }

std::string pathText(const Graph& g, const std::vector<int>& path) {
    std::string out;
    for (size_t i = 0; i < path.size(); ++i) {
        if (i > 0) out += " -> ";
        out += g.idOf(path[i]);
    }
    return out;
}

std::string jsonString(const std::string& s) {
    std::string out = "\"";
    for (char c : s) {
        if (c == '"' || c == '\\') out += '\\';
        out += c;
    }
    return out + "\"";
}

std::string jsonNumber(double x) {
    if (x == INF) return "null";
    std::ostringstream o;
    o << std::setprecision(10) << round3(x);
    return o.str();
}

// ---------------------------------------------------------------- text report

void printAllocationRow(const Dataset& data, const Allocation& a) {
    const Package& p = data.packages[a.lot];
    std::cout << "    " << std::left << std::setw(5) << p.id << std::setw(26) << p.item << std::setw(9) << p.category
              << "-> " << std::setw(5) << data.ngos[a.ngo].id << std::right << std::setw(5) << a.kg << " kg"
              << std::setw(7) << fixedText(a.km, 1) << " km" << std::setw(12) << rupees(a.cost_paise) << "\n";
}

void printMetrics(const Metrics& m, const Metrics& g) {
    const auto row = [](const std::string& label, const std::string& a, const std::string& b) {
        std::cout << "  " << std::left << std::setw(40) << label << std::right << std::setw(12) << a << std::setw(12)
                  << b << "\n";
    };
    std::cout << "\n== Metrics: MCMF vs Greedy baseline (identical data and constraints) ==\n";
    row("", "MCMF", "Greedy");
    row("Total donor supply (kg)", numText(m.supplyKg), numText(g.supplyKg));
    row("Total NGO demand (kg)", numText(m.demandKg), numText(g.demandKg));
    row("Allocated (kg)", numText(m.allocatedKg), numText(g.allocatedKg));
    row("DF  demand fulfilment (%)", fixedText(m.demandFulfilledPct, 1), fixedText(g.demandFulfilledPct, 1));
    row("US  unallocated surplus (kg)", numText(m.unallocatedSurplusKg), numText(g.unallocatedSurplusKg));
    row("Delivered before expiry (kg)", numText(m.deliveredKg), numText(g.deliveredKg));
    row("EF  expiry-feasible delivery (%)", fixedText(m.expiryFeasiblePct, 1), fixedText(g.expiryFeasiblePct, 1));
    row("TC  allocation cost, sum x*D*R", rupees(m.allocationCostPaise), rupees(g.allocationCostPaise));
    row("Trip cost (round trips x Rs/km)", rupees(m.tripCostPaise), rupees(g.tripCostPaise));
    row("Trips / low-load trips", numText(m.trips) + " / " + numText(m.lowLoadTrips),
        numText(g.trips) + " / " + numText(g.lowLoadTrips));
    row("Average trip load (%)", fixedText(m.avgUtilisationPct, 1), fixedText(g.avgUtilisationPct, 1));
    row("ET  allocation time (ms, average)", fixedText(m.allocationMs, 4), fixedText(g.allocationMs, 4));
}

void printText(const Report& r) {
    const Dataset& data = r.data;
    const DistanceMatrix& m = r.matrix;
    std::cout << std::fixed << std::setprecision(1);
    std::cout << "FoodLoop report for " << r.dir << "\n\n";
    std::cout << "Road network: " << r.graph.size() << " junctions, " << r.graph.edgeCount() << " roads\n\n";

    std::cout << "== Stage 2: Dijkstra distance matrix (km) ==\n" << std::setw(8) << "";
    for (const std::string& n : m.ngoIds) std::cout << std::setw(8) << n;
    std::cout << "\n";
    for (size_t d = 0; d < m.donorIds.size(); ++d) {
        std::cout << std::setw(8) << m.donorIds[d];
        for (double km : m.km[d]) {
            if (km == INF) std::cout << std::setw(8) << "-";
            else std::cout << std::setw(8) << km;
        }
        std::cout << "\n";
    }

    std::cout << "\nRoutes\n";
    for (size_t d = 0; d < m.donorIds.size(); ++d) {
        for (size_t n = 0; n < m.ngoIds.size(); ++n) {
            std::cout << "  " << m.donorIds[d] << " -> " << std::left << std::setw(4) << m.ngoIds[n] << std::right;
            if (m.km[d][n] == INF) {
                std::cout << "  unreachable\n";
                continue;
            }
            std::cout << std::setw(6) << m.km[d][n] << " km   "
                      << pathText(r.graph, m.route(static_cast<int>(d), static_cast<int>(n))) << "\n";
        }
    }

    std::cout << "\n== Stage 3: Greedy expiry ranking and tiers ==\n";
    std::cout << std::left << std::setw(6) << "Rank" << std::setw(6) << "Lot" << std::setw(7) << "Donor"
              << std::setw(26) << "Item" << std::setw(9) << "Category" << std::right << std::setw(5) << "Kg"
              << std::setw(12) << "Expiry (h)" << std::setw(6) << "Tier" << "\n";
    for (size_t i = 0; i < r.priority.size(); ++i) {
        const Package& p = r.priority[i];
        std::cout << std::left << std::setw(6) << (i + 1) << std::setw(6) << p.id << std::setw(7) << p.donor_id
                  << std::setw(26) << p.item << std::setw(9) << p.category << std::right << std::setw(5)
                  << p.weight_kg << std::setw(12) << fixedText(p.expiry_hours, 2) << std::setw(6)
                  << urgencyTier(p.expiry_hours, r.params.tier_boundaries_hours) + 1 << "\n";
    }

    std::cout << "\n== Feasibility (category, reachability, expiry; handling " << r.params.handling_hours
              << " h; R = Rs " << fixedText(r.params.rate_inr_per_kg_km, 2) << " per kg-km) ==\n";
    std::cout << "  pairs kept " << r.feas.keptPairs << ", removed: incompatible " << r.feas.removedIncompatible
              << ", unreachable " << r.feas.removedUnreachable << ", too late " << r.feas.removedExpiry
              << "; lots already expired " << r.feas.expiredLots << "\n";
    for (size_t k = 0; k < data.packages.size(); ++k) {
        const Package& p = data.packages[k];
        std::cout << "  " << std::left << std::setw(5) << p.id << std::setw(9) << p.category << std::right
                  << std::setw(6) << fixedText(p.expiry_hours, 2) << " h  ->";
        if (r.feas.byLot[k].empty()) std::cout << " (no feasible NGO)";
        for (const LotRoute& lr : r.feas.byLot[k])
            std::cout << " " << data.ngos[lr.ngo].id << "(" << rupees(lr.cost_paise_per_kg) << "/kg)";
        std::cout << "\n";
    }

    std::cout << "\n== Stage 4: Min Cost Max Flow allocation, tier by tier ==\n";
    for (const TierResult& t : r.plan.tiers) {
        std::cout << "Tier " << t.tier + 1 << ": " << t.lots.size() << " lots, supply " << t.supplyKg
                  << " kg -> placed " << t.flowKg << " kg, cost " << rupees(t.costPaise) << "  ["
                  << t.networkNodes << " nodes, " << t.networkEdges << " edges, " << t.augmentations
                  << " augmenting paths]\n";
        for (const Allocation& a : r.plan.allocations)
            if (a.tier == t.tier) printAllocationRow(data, a);
    }

    std::cout << "\nUnallocated surplus\n";
    bool any = false;
    for (size_t k = 0; k < data.packages.size(); ++k) {
        if (r.plan.unplacedKg[k] == 0) continue;
        any = true;
        std::cout << "  " << std::left << std::setw(5) << data.packages[k].id << std::setw(26)
                  << data.packages[k].item << std::setw(9) << data.packages[k].category << std::right
                  << std::setw(5) << r.plan.unplacedKg[k] << " kg\n";
    }
    if (!any) std::cout << "  none\n";

    std::cout << "\nUnmet demand\n";
    any = false;
    for (size_t j = 0; j < data.ngos.size(); ++j) {
        for (const std::string& cat : categories()) {
            const long long kg = r.plan.unmetKg[j].at(cat);
            if (kg == 0) continue;
            any = true;
            std::cout << "  " << std::left << std::setw(5) << data.ngos[j].id << std::setw(28) << data.ngos[j].name
                      << std::setw(9) << cat << std::right << std::setw(5) << kg << " kg\n";
        }
    }
    if (!any) std::cout << "  none\n";

    std::cout << "\nGreedy baseline allocation (expiry order -> nearest feasible NGO)\n";
    for (const Allocation& a : r.greedyPlan.allocations) printAllocationRow(data, a);

    std::cout << "\n== Stage 5: Vehicle trips for the MCMF plan (crates of " << r.params.crate_kg
              << " kg, Knapsack per trip, shift " << r.params.shift_hours << " h) ==\n";
    for (size_t v = 0; v < data.vehicles.size(); ++v) {
        const Vehicle& veh = data.vehicles[v];
        std::cout << veh.donor_id << " " << veh.type << " (" << veh.capacity_kg << " kg, "
                  << fixedText(veh.speed_kmph, 0) << " km/h, Rs " << fixedText(veh.cost_per_km_inr, 2) << "/km)\n";
        any = false;
        for (const Trip& t : r.delivery.trips) {
            if (t.vehicle != static_cast<int>(v)) continue;
            any = true;
            std::cout << "  Trip " << t.number << "  depart " << std::setw(5) << fixedText(t.departHours, 2)
                      << " h -> " << std::left << std::setw(4) << data.ngos[t.ngo].id << std::right
                      << " served by " << std::setw(5) << fixedText(t.servedByHours, 2) << " h, back "
                      << std::setw(5) << fixedText(t.returnHours, 2) << " h   load " << std::setw(3) << t.loadKg
                      << "/" << t.capacityKg << " kg (" << std::setw(3) << fixedText(utilisationPct(t), 0)
                      << "%)   " << rupees(t.costPaise)
                      << (utilisationPct(t) < r.params.low_load_pct ? "   LOW LOAD" : "") << "\n";
            std::cout << "          crates:";
            for (int c : t.crates)
                std::cout << " " << crateLabel(data, r.delivery.crates[c]) << " " << r.delivery.crates[c].kg << "kg";
            std::cout << "\n";
        }
        if (!any) std::cout << "  no trips\n";
    }

    std::cout << "\nUndelivered crates\n";
    for (const Undelivered& u : r.delivery.undelivered) {
        const Crate& c = r.delivery.crates[u.crate];
        std::cout << "  " << std::left << std::setw(7) << crateLabel(data, c) << "-> " << std::setw(5)
                  << data.ngos[c.ngo].id << std::right << std::setw(4) << c.kg << " kg   " << u.reason << "\n";
    }
    if (r.delivery.undelivered.empty()) std::cout << "  none\n";

    printMetrics(r.metrics, r.greedyMetrics);

    const StageTimes& s = r.stageMs;
    std::cout << "\n== Stage times (ms, averaged) ==\n"
              << "  Dijkstra " << fixedText(s.dijkstra, 4) << " | feasibility " << fixedText(s.feasibility, 4)
              << " | Greedy ranking " << fixedText(s.greedyRanking, 4) << " | MCMF " << fixedText(s.mcmf, 4)
              << " | Knapsack trips " << fixedText(s.trips, 4) << " | Greedy allocation "
              << fixedText(s.greedyAllocation, 4) << "\n";
}

// ---------------------------------------------------------------- JSON

void jsonAllocations(std::ostream& o, const Dataset& data, const AllocationPlan& plan) {
    o << "[";
    for (size_t i = 0; i < plan.allocations.size(); ++i) {
        const Allocation& a = plan.allocations[i];
        const Package& p = data.packages[a.lot];
        o << (i ? ",\n" : "\n") << "    {\"tier\": " << a.tier + 1 << ", \"package_id\": " << jsonString(p.id)
          << ", \"donor_id\": " << jsonString(p.donor_id) << ", \"ngo_id\": " << jsonString(data.ngos[a.ngo].id)
          << ", \"category\": " << jsonString(p.category) << ", \"kg\": " << a.kg << ", \"km\": " << jsonNumber(a.km)
          << ", \"cost_paise\": " << a.cost_paise << "}";
    }
    o << "\n  ]";
}

void jsonMetrics(std::ostream& o, const Metrics& m) {
    o << "{\"supply_kg\": " << m.supplyKg << ", \"demand_kg\": " << m.demandKg
      << ", \"allocated_kg\": " << m.allocatedKg << ", \"demand_fulfilled_pct\": " << jsonNumber(m.demandFulfilledPct)
      << ", \"unallocated_surplus_kg\": " << m.unallocatedSurplusKg << ", \"delivered_kg\": " << m.deliveredKg
      << ", \"undelivered_kg\": " << m.undeliveredKg
      << ", \"expiry_feasible_pct\": " << jsonNumber(m.expiryFeasiblePct)
      << ", \"allocation_cost_paise\": " << m.allocationCostPaise
      << ", \"allocation_cost_inr\": " << jsonNumber(m.allocationCostPaise / 100.0)
      << ", \"trip_cost_paise\": " << m.tripCostPaise << ", \"trip_cost_inr\": " << jsonNumber(m.tripCostPaise / 100.0)
      << ", \"trips\": " << m.trips << ", \"low_load_trips\": " << m.lowLoadTrips
      << ", \"avg_utilisation_pct\": " << jsonNumber(m.avgUtilisationPct)
      << ", \"execution_ms\": " << std::setprecision(6) << m.allocationMs << "}";
}

void printJson(const Report& r) {
    const Dataset& data = r.data;
    const DistanceMatrix& m = r.matrix;
    std::ostringstream o;

    o << "{\n  \"dataset\": " << jsonString(r.dir) << ",\n";
    o << "  \"params\": {\"handling_hours\": " << jsonNumber(r.params.handling_hours)
      << ", \"tier_boundaries_hours\": [";
    for (size_t i = 0; i < r.params.tier_boundaries_hours.size(); ++i)
        o << (i ? ", " : "") << jsonNumber(r.params.tier_boundaries_hours[i]);
    o << "], \"crate_kg\": " << r.params.crate_kg << ", \"tier_weights\": [";
    for (size_t i = 0; i < r.params.tier_weights.size(); ++i) o << (i ? ", " : "") << r.params.tier_weights[i];
    o << "], \"shift_hours\": " << jsonNumber(r.params.shift_hours)
      << ", \"low_load_pct\": " << jsonNumber(r.params.low_load_pct)
      << ", \"rate_inr_per_kg_km\": " << jsonNumber(r.params.rate_inr_per_kg_km) << "},\n";
    o << "  \"road_network\": {\"junctions\": " << r.graph.size() << ", \"roads\": " << r.graph.edgeCount()
      << "},\n";

    o << "  \"distance_matrix\": {\n    \"donors\": [";
    for (size_t d = 0; d < m.donorIds.size(); ++d) o << (d ? ", " : "") << jsonString(m.donorIds[d]);
    o << "],\n    \"ngos\": [";
    for (size_t n = 0; n < m.ngoIds.size(); ++n) o << (n ? ", " : "") << jsonString(m.ngoIds[n]);
    o << "],\n    \"km\": [";
    for (size_t d = 0; d < m.km.size(); ++d) {
        o << (d ? ",\n           [" : "[");
        for (size_t n = 0; n < m.km[d].size(); ++n) o << (n ? ", " : "") << jsonNumber(m.km[d][n]);
        o << "]";
    }
    o << "]\n  },\n";

    o << "  \"routes\": [";
    bool first = true;
    for (size_t d = 0; d < m.donorIds.size(); ++d) {
        for (size_t n = 0; n < m.ngoIds.size(); ++n) {
            const std::vector<int> path = m.route(static_cast<int>(d), static_cast<int>(n));
            o << (first ? "\n" : ",\n") << "    {\"donor\": " << jsonString(m.donorIds[d])
              << ", \"ngo\": " << jsonString(m.ngoIds[n]) << ", \"km\": " << jsonNumber(m.km[d][n])
              << ", \"path\": [";
            for (size_t i = 0; i < path.size(); ++i) o << (i ? ", " : "") << jsonString(r.graph.idOf(path[i]));
            o << "]}";
            first = false;
        }
    }
    o << "\n  ],\n";

    o << "  \"priority_list\": [";
    for (size_t i = 0; i < r.priority.size(); ++i) {
        const Package& p = r.priority[i];
        o << (i ? ",\n" : "\n") << "    {\"rank\": " << (i + 1) << ", \"package_id\": " << jsonString(p.id)
          << ", \"donor_id\": " << jsonString(p.donor_id) << ", \"item\": " << jsonString(p.item)
          << ", \"category\": " << jsonString(p.category) << ", \"kg\": " << p.weight_kg
          << ", \"meals\": " << p.meals << ", \"expiry_hours\": " << jsonNumber(p.expiry_hours)
          << ", \"tier\": " << urgencyTier(p.expiry_hours, r.params.tier_boundaries_hours) + 1 << "}";
    }
    o << "\n  ],\n";

    o << "  \"feasibility\": {\"kept\": " << r.feas.keptPairs
      << ", \"removed_incompatible\": " << r.feas.removedIncompatible
      << ", \"removed_unreachable\": " << r.feas.removedUnreachable
      << ", \"removed_expiry\": " << r.feas.removedExpiry << ", \"expired_lots\": " << r.feas.expiredLots
      << ",\n    \"by_lot\": [";
    for (size_t k = 0; k < data.packages.size(); ++k) {
        o << (k ? ",\n" : "\n") << "      {\"package_id\": " << jsonString(data.packages[k].id) << ", \"routes\": [";
        for (size_t i = 0; i < r.feas.byLot[k].size(); ++i) {
            const LotRoute& lr = r.feas.byLot[k][i];
            o << (i ? ", " : "") << "{\"ngo_id\": " << jsonString(data.ngos[lr.ngo].id)
              << ", \"km\": " << jsonNumber(lr.km) << ", \"travel_hours\": " << jsonNumber(lr.travel_hours)
              << ", \"cost_paise_per_kg\": " << lr.cost_paise_per_kg << "}";
        }
        o << "]}";
    }
    o << "\n    ]\n  },\n";

    o << "  \"tiers\": [";
    for (size_t i = 0; i < r.plan.tiers.size(); ++i) {
        const TierResult& t = r.plan.tiers[i];
        o << (i ? ",\n" : "\n") << "    {\"tier\": " << t.tier + 1 << ", \"lots\": [";
        for (size_t k = 0; k < t.lots.size(); ++k) o << (k ? ", " : "") << jsonString(data.packages[t.lots[k]].id);
        o << "], \"supply_kg\": " << t.supplyKg << ", \"flow_kg\": " << t.flowKg
          << ", \"cost_paise\": " << t.costPaise << ", \"augmentations\": " << t.augmentations
          << ", \"nodes\": " << t.networkNodes << ", \"edges\": " << t.networkEdges << "}";
    }
    o << "\n  ],\n";

    o << "  \"allocations\": ";
    jsonAllocations(o, data, r.plan);
    o << ",\n  \"greedy_allocations\": ";
    jsonAllocations(o, data, r.greedyPlan);
    o << ",\n";

    o << "  \"unallocated\": [";
    first = true;
    for (size_t k = 0; k < data.packages.size(); ++k) {
        if (r.plan.unplacedKg[k] == 0) continue;
        o << (first ? "\n" : ",\n") << "    {\"package_id\": " << jsonString(data.packages[k].id)
          << ", \"kg\": " << r.plan.unplacedKg[k] << "}";
        first = false;
    }
    o << "\n  ],\n";

    o << "  \"unmet_demand\": [";
    first = true;
    for (size_t j = 0; j < data.ngos.size(); ++j) {
        for (const std::string& cat : categories()) {
            const long long kg = r.plan.unmetKg[j].at(cat);
            if (kg == 0) continue;
            o << (first ? "\n" : ",\n") << "    {\"ngo_id\": " << jsonString(data.ngos[j].id)
              << ", \"category\": " << jsonString(cat) << ", \"kg\": " << kg << "}";
            first = false;
        }
    }
    o << "\n  ],\n";

    const DeliveryPlan& dl = r.delivery;
    o << "  \"delivery\": {\"trips\": [";
    for (size_t i = 0; i < dl.trips.size(); ++i) {
        const Trip& t = dl.trips[i];
        o << (i ? ",\n" : "\n") << "      {\"donor_id\": " << jsonString(data.donors[t.donor].id)
          << ", \"vehicle_id\": " << jsonString(data.vehicles[t.vehicle].id) << ", \"trip\": " << t.number
          << ", \"ngo_id\": " << jsonString(data.ngos[t.ngo].id) << ", \"depart_hours\": " << jsonNumber(t.departHours)
          << ", \"served_by_hours\": " << jsonNumber(t.servedByHours)
          << ", \"return_hours\": " << jsonNumber(t.returnHours) << ", \"load_kg\": " << t.loadKg
          << ", \"capacity_kg\": " << t.capacityKg << ", \"utilisation_pct\": " << jsonNumber(utilisationPct(t))
          << ", \"low_load\": " << (utilisationPct(t) < r.params.low_load_pct ? "true" : "false")
          << ", \"km_one_way\": " << jsonNumber(t.km) << ", \"cost_paise\": " << t.costPaise << ", \"crates\": [";
        for (size_t k = 0; k < t.crates.size(); ++k) {
            const Crate& c = dl.crates[t.crates[k]];
            o << (k ? ", " : "") << "{\"crate\": " << jsonString(crateLabel(data, c))
              << ", \"package_id\": " << jsonString(data.packages[c.lot].id) << ", \"kg\": " << c.kg
              << ", \"tier\": " << c.tier + 1 << "}";
        }
        o << "]}";
    }
    o << "\n    ],\n    \"undelivered\": [";
    for (size_t i = 0; i < dl.undelivered.size(); ++i) {
        const Crate& c = dl.crates[dl.undelivered[i].crate];
        o << (i ? ",\n" : "\n") << "      {\"crate\": " << jsonString(crateLabel(data, c))
          << ", \"package_id\": " << jsonString(data.packages[c.lot].id)
          << ", \"ngo_id\": " << jsonString(data.ngos[c.ngo].id) << ", \"kg\": " << c.kg
          << ", \"reason\": " << jsonString(dl.undelivered[i].reason) << "}";
    }
    o << "\n    ]\n  },\n";

    o << "  \"metrics\": {\n    \"mcmf\": ";
    jsonMetrics(o, r.metrics);
    o << ",\n    \"greedy\": ";
    jsonMetrics(o, r.greedyMetrics);
    o << "\n  },\n";

    const StageTimes& s = r.stageMs;
    o << std::setprecision(6) << "  \"timings_ms\": {\"dijkstra\": " << s.dijkstra
      << ", \"feasibility\": " << s.feasibility << ", \"greedy_ranking\": " << s.greedyRanking
      << ", \"mcmf\": " << s.mcmf << ", \"knapsack_trips\": " << s.trips
      << ", \"greedy_allocation\": " << s.greedyAllocation << "}\n}\n";

    std::cout << o.str();
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "usage: foodloop <dataset_dir> [--json] [--params config/params.csv] [--repeat N]\n";
        return 2;
    }
    Report r;
    r.dir = argv[1];
    bool json = false;
    int repeat = 20;
    std::string paramsPath = "config/params.csv";
    for (int i = 2; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--json") json = true;
        else if (arg == "--params" && i + 1 < argc) paramsPath = argv[++i];
        else if (arg == "--repeat" && i + 1 < argc) repeat = std::atoi(argv[++i]);
        else {
            std::cerr << "unknown argument: " << arg << "\n";
            return 2;
        }
    }
    if (repeat < 1) {
        std::cerr << "--repeat must be at least 1\n";
        return 2;
    }

    try {
        r.data = loadDataset(r.dir);  // includes validateDataset
        r.params = loadParams(paramsPath);
        r.graph = buildRoadGraph(r.data);
        r.stageMs.dijkstra = averageMs([&] { r.matrix = computeDistanceMatrix(r.data, r.graph); }, repeat);
        r.stageMs.feasibility = averageMs([&] { r.feas = computeFeasibility(r.data, r.matrix, r.params); }, repeat);
        r.stageMs.greedyRanking = averageMs([&] { r.priority = expiryPriorityList(r.data.packages); }, repeat);

        r.stageMs.mcmf = averageMs([&] { r.plan = allocateByTiers(r.data, r.feas, r.params); }, repeat);
        r.stageMs.trips = averageMs([&] { r.delivery = planTrips(r.data, r.plan, r.params); }, repeat);
        r.metrics = computeMetrics(r.plan, r.delivery, r.params, r.stageMs.mcmf);

        r.stageMs.greedyAllocation = averageMs([&] { r.greedyPlan = allocateGreedy(r.data, r.feas, r.params); }, repeat);
        r.greedyDelivery = planTrips(r.data, r.greedyPlan, r.params);
        r.greedyMetrics = computeMetrics(r.greedyPlan, r.greedyDelivery, r.params, r.stageMs.greedyAllocation);

        if (json) printJson(r);
        else printText(r);
    } catch (const std::exception& e) {
        std::cerr << "error: " << e.what() << "\n";
        return 1;
    }
    return 0;
}
