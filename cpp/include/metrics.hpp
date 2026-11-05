// Performance evaluation, computed the same way for the MCMF plan and the Greedy baseline.
//
//   DF = total allocated / total NGO demand * 100          demand fulfilment
//   US = total donor supply - total allocated              unallocated surplus
//   EF = kg delivered before expiry / total allocated * 100 expiry-feasible delivery
//   TC = sum x_ij * D_ij * R                               allocation transport cost
//   ET = T_end - T_start of the allocation stage, averaged over repeated runs
#pragma once

#include "allocation.hpp"
#include "data.hpp"
#include "trips.hpp"

namespace foodloop {

struct Metrics {
    long long supplyKg;
    long long demandKg;
    long long allocatedKg;
    double demandFulfilledPct;       // DF
    long long unallocatedSurplusKg;  // US
    long long deliveredKg;           // served before expiry and within the shift (Stage 5)
    long long undeliveredKg;
    double expiryFeasiblePct;        // EF
    long long allocationCostPaise;   // TC
    long long tripCostPaise;         // round trips * vehicle Rs/km (Stage 5)
    int trips;
    int lowLoadTrips;
    double avgUtilisationPct;
    double allocationMs;             // ET
};

Metrics computeMetrics(const AllocationPlan& plan, const DeliveryPlan& delivery, const Params& params,
                       double allocationMs);

}  // namespace foodloop
