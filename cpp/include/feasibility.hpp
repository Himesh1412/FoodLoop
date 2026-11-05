// Stage 2: which (lot, NGO) pairs are allowed, and what they cost.
// A lot k at donor i may go to NGO j only if
//   - compatibility: NGO j has demand for the lot's category,
//   - reachability:  there is a road path from i to j,
//   - expiry:        travel_time(i, j) + handling_hours <= expiry of k.
#pragma once

#include <vector>

#include "data.hpp"
#include "dijkstra.hpp"

namespace foodloop {

struct LotRoute {
    int ngo;                   // index into Dataset::ngos
    double km;                 // shortest road distance d_ij
    double travel_hours;       // t_ij = d_ij / vehicle speed
    long long cost_paise_per_kg;  // C_ij = D_ij * R, in paise (integer for exact Bellman-Ford)
};

struct Feasibility {
    std::vector<std::vector<LotRoute>> byLot;  // byLot[k] = allowed NGOs for lot k (index into Dataset::packages)

    // How many (lot, NGO) pairs each rule removed, for the report. Checked in this order.
    int keptPairs = 0;
    int removedIncompatible = 0;
    int removedUnreachable = 0;
    int removedExpiry = 0;
    int expiredLots = 0;  // lots with no shelf life left at planning time (expiry_hours == 0)
};

// Index of the vehicle owned by each donor, in Dataset::donors order. Throws unless each donor has exactly one.
std::vector<int> vehicleOfDonor(const Dataset& data);

Feasibility computeFeasibility(const Dataset& data, const DistanceMatrix& m, const Params& params);

}  // namespace foodloop
