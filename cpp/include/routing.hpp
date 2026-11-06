// Stage 5 (fleet mode): multi-stop vehicle routing and joint allocation-routing.
//
// Enhancement 3 - multi-stop routing. Every MCMF allocation (lot k -> NGO j, q kg) becomes a
// pickup-and-delivery request. Requests are taken most urgent first (earliest pickup close /
// NGO close / expiry) and inserted by cheapest feasible insertion: for every vehicle route and
// every pair of positions (pickup before delivery) the new route is simulated, and the insertion
// is kept only if every stop meets its time window, every lot is delivered before expiry, the
// load never exceeds capacity after any stop (enhancement 6) and the vehicle is back at its depot
// before the end of the shift. Among feasible insertions the one that carries the most kg wins,
// then the smallest added cost (distance cost, plus the hiring cost if the vehicle was idle).
// If only part of a request fits (capacity), that part is inserted and the rest is queued again,
// so pickups and deliveries are split across vehicles and visits (enhancement 7).
//
// Enhancement 9 - joint allocation and routing (iterative feedback). The allocation assumes a
// vehicle can be anywhere; routing may then fail some (lot, NGO) pairs. planJointly repeats
//   allocate (MCMF) -> route (+ top-up re-allocation of leftovers into the same routes)
//   -> remove the pairs that could not be routed from the network
// for three insertion orders, keeps the plan with the best base-paper objective, and finally
// improves it by ruin and recreate (remove a few related requests, re-insert them with the
// unserved ones, keep only improvements). The objective is
//   routing cost + hiring cost x vehicles used + unmet penalty x (unmet pickup + unmet delivery).
#pragma once

#include <string>
#include <vector>

#include "allocation.hpp"
#include "data.hpp"
#include "feasibility.hpp"
#include "graph.hpp"

namespace foodloop {

struct RouteStop {
    bool pickup;      // true = pickup at a donor, false = delivery at an NGO
    int request;      // index into RoutePlan::requests
    int kg;
    double arriveHours = 0;
    double startHours = 0;   // service start, after waiting for the window to open
    double departHours = 0;
    int loadAfterKg = 0;     // vehicle load when leaving this stop
};

struct Route {
    int vehicle;  // index into Dataset::vehicles
    std::vector<RouteStop> stops;
    double km = 0;           // depot -> stops -> depot
    long long costPaise = 0; // km x vehicle Rs/km
    double endHours = 0;     // back at the depot
    int maxLoadKg = 0;
    int deliveredKg = 0;
    int capacityKg = 0;
};

struct Request {
    int lot;   // index into Dataset::packages
    int ngo;   // index into Dataset::ngos
    int tier;
    int kg;    // allocated quantity
    int servedKg = 0;
};

struct RoutePlan {
    std::vector<Request> requests;
    std::vector<Route> routes;   // one per vehicle; empty stops = vehicle not hired
    long long deliveredKg = 0;
    long long unservedKg = 0;    // allocated but not routed
    long long costPaise = 0;     // routing cost of all routes
    long long fixedCostPaise = 0;
    int vehiclesUsed = 0;
    double km = 0;
    double objective = 0;        // base-paper objective (see above), in rupees
    long long unmetUnits = 0;    // (supply - delivered) + (demand - delivered)
    int feedbackRounds = 1;      // allocation-routing rounds used by planJointly
    int blockedPairs = 0;        // (lot, NGO) pairs removed by the feedback loop
};

// Multi-stop routing of an allocation (enhancements 3, 5, 6, 7, 8).
RoutePlan routeAllocation(const Dataset& data, const Graph& g, const AllocationPlan& plan, const Params& params);

// Joint allocation and routing by iterative feedback (enhancement 9). Returns the best plan and
// sets `bestAllocation` to the allocation it routes.
RoutePlan planJointly(const Dataset& data, const Graph& g, const Feasibility& feas, const Params& params,
                      AllocationPlan& bestAllocation, int maxRounds = 8,
                      int improveIterations = 300);

// Base-paper objective for a routed plan; fills plan.objective and plan.unmetUnits.
void scoreRoutePlan(const Dataset& data, const Params& params, RoutePlan& plan);

}  // namespace foodloop
