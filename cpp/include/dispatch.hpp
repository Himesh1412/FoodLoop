// Stage 5 (fleet mode): schedule shared vehicles for the allocated food.
//
// Enhancements over the donor-vehicle trip planner (trips.hpp):
//   1. vehicles start and finish at independent depots / volunteer locations;
//   2. any vehicle can serve any donor -> NGO job (dynamic assignment);
//   4. food-ready times and pickup windows: a vehicle waits if it arrives early and
//      cannot collect after the pickup window closes;
//   5. arrival, service start and departure are recorded at every pickup and delivery,
//      with optional NGO opening windows;
//   6. vehicle load is tracked after every pickup and every delivery;
//   7. split service: one lot's crates can travel on different vehicles and to different NGOs;
//   8. objective: the most urgent food is scheduled first (on time before everything else),
//      then the cheapest vehicle that still delivers it on time is chosen.
// Every trip is a direct job: current location -> donor (pickup) -> NGO (delivery). A vehicle
// chains jobs back to back and returns to its depot at the end. Multi-stop routing and joint
// allocation-routing are out of scope (future work).
//
// Algorithm, repeated until no job can be scheduled:
//   a. group the waiting crates by (donor, NGO);
//   b. for every group and every vehicle, simulate the job: drive to the donor, wait for the
//      earliest pickup time at which some crate is ready, then choose that trip's crates with a
//      0/1 Knapsack (value = kg x tier weight) among the crates that would still be served
//      before expiry, inside the NGO window and with time to return before the shift ends;
//   c. Greedy, Earliest Deadline First: take the group holding the most urgent schedulable crate,
//      where a crate's deadline is the earlier of its pickup-window close and its expiry;
//   d. choose the vehicle among the options that carry that most urgent crate:
//        COST_AWARE      cheapest job (ties: earlier delivery),
//        BALANCED        cheapest job + time weight x delivery finish time,
//        NEAREST_VEHICLE earliest pickup arrival (the usual dispatch rule, used as the baseline);
//   THROUGHPUT skips step c and takes, over all groups, the job with the most urgency-weighted kg
//      per hour of vehicle time (ties: cheaper), so vehicles spend less time driving empty.
// dispatchBest (the proposed dispatcher) runs COST_AWARE, two BALANCED weights, THROUGHPUT and
// NEAREST_VEHICLE and keeps the plan that delivers the most kg before expiry, then the most urgency-weighted value,
// then the lowest cost: on-time delivery first, cost second (enhancement 8).
#pragma once

#include <string>
#include <vector>

#include "allocation.hpp"
#include "data.hpp"
#include "graph.hpp"
#include "trips.hpp"

namespace foodloop {

enum class DispatchPolicy { COST_AWARE, BALANCED, NEAREST_VEHICLE, THROUGHPUT };

// One stop of a job, for the schedule table.
struct Stop {
    std::string kind;   // "pickup" or "delivery"
    std::string place;  // donor or NGO id
    double arriveHours;
    double serviceStartHours;  // after any wait for food-ready time or NGO opening
    double departHours;
    int loadAfterKg;           // vehicle load when leaving this stop
};

struct FleetTrip {
    int vehicle;  // index into Dataset::vehicles
    int number;   // 1, 2, ... per vehicle
    int donor;    // index into Dataset::donors
    int ngo;      // index into Dataset::ngos
    std::string fromPlace;   // depot id or NGO id the vehicle came from
    double leaveHours;       // left the previous location
    Stop pickup;
    Stop delivery;
    double waitHours;        // idle time at pickup and delivery
    double emptyKm;          // previous location -> donor
    double loadedKm;         // donor -> NGO
    std::vector<int> crates; // indices into FleetPlan::crates
    int loadKg;
    int capacityKg;
    long long costPaise;     // (emptyKm + loadedKm) x vehicle Rs/km
};

struct VehicleDay {
    int vehicle;
    int trips = 0;
    double endHours = 0;      // back at the depot
    double km = 0;            // including the final return
    long long costPaise = 0;  // including the final return
    int deliveredKg = 0;
};

struct FleetPlan {
    std::vector<Crate> crates;
    std::vector<FleetTrip> trips;
    std::vector<VehicleDay> vehicles;
    std::vector<Undelivered> undelivered;
    long long deliveredKg = 0;
    long long undeliveredKg = 0;
    long long costPaise = 0;  // all legs including returns to depots
    double km = 0;
    long long deliveredValue = 0;  // sum of kg x tier weight delivered
    int vehiclesUsed = 0;
    long long fixedCostPaise = 0;  // vehiclesUsed x params.vehicle_fixed_cost_inr
    std::string policy;            // which dispatch rule produced this plan
};

// Shortest road distance between any two junctions used by depots, donors and NGOs.
class NodeDistances {
public:
    NodeDistances(const Dataset& data, const Graph& g);
    double km(const std::string& fromNode, const std::string& toNode) const;

private:
    const Graph* g_;
    std::vector<int> sourceOf_;          // junction index -> row in runs_, or -1
    std::vector<std::vector<double>> runs_;
};

// One dispatch rule. timeWeightPaisePerHour is used only by BALANCED.
FleetPlan dispatchFleet(const Dataset& data, const Graph& g, const AllocationPlan& plan, const Params& params,
                        DispatchPolicy policy = DispatchPolicy::COST_AWARE, double timeWeightPaisePerHour = 0);

// Proposed dispatcher: best plan over the rule portfolio (most on-time kg, then value, then cost).
FleetPlan dispatchBest(const Dataset& data, const Graph& g, const AllocationPlan& plan, const Params& params);

// True if plan a is better than plan b under the on-time-first, cost-second objective.
bool betterFleetPlan(const FleetPlan& a, const FleetPlan& b);

double utilisationPct(const FleetTrip& t);

}  // namespace foodloop
