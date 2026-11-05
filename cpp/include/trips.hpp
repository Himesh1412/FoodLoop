// Stage 5: turn the allocation into vehicle trips.
//
// For each donor's vehicle, starting at clock = 0:
//   1. drop crates that can no longer be served in time if the vehicle leaves now
//      (reason "expired"), or that would be served after the shift ends ("shift over");
//   2. Greedy (Earliest Deadline First): the next trip goes to the NGO of the crate
//      with the earliest latest-departure time;
//   3. 0/1 Knapsack: choose that NGO's crates for the trip within vehicle capacity;
//   4. drive there, unload, return: clock += 2 * travel + handling.
// One NGO per trip (direct delivery, no multi-stop routing).
#pragma once

#include <string>
#include <vector>

#include "allocation.hpp"
#include "data.hpp"

namespace foodloop {

struct Crate {
    int lot;   // index into Dataset::packages
    int seq;   // 1, 2, ... per lot, for labels such as "P1#2"
    int ngo;   // destination, index into Dataset::ngos
    int tier;  // 0-based urgency tier
    int kg;
    double km;        // one-way road distance donor -> NGO
    long long value;  // kg * tier weight, maximised by the Knapsack
};

struct Trip {
    int donor;    // index into Dataset::donors
    int vehicle;  // index into Dataset::vehicles
    int number;   // 1, 2, ... per vehicle
    int ngo;
    double departHours;
    double servedByHours;  // depart + travel + handling
    double returnHours;    // depart + 2 * travel + handling
    std::vector<int> crates;  // indices into DeliveryPlan::crates
    int loadKg;
    int capacityKg;
    double km;              // one way
    long long costPaise;    // round trip: 2 * km * cost per km
};

struct Undelivered {
    int crate;
    std::string reason;  // "expired", "shift over" or "too heavy"
};

struct DeliveryPlan {
    std::vector<Crate> crates;
    std::vector<Trip> trips;
    std::vector<Undelivered> undelivered;
    long long deliveredKg = 0;
    long long undeliveredKg = 0;
    long long costPaise = 0;
};

// Splits every allocation into crates of at most params.crate_kg.
std::vector<Crate> makeCrates(const AllocationPlan& plan, const Params& params);

std::string crateLabel(const Dataset& data, const Crate& c);

double utilisationPct(const Trip& t);

DeliveryPlan planTrips(const Dataset& data, const AllocationPlan& plan, const Params& params);

}  // namespace foodloop
