#include "metrics.hpp"

namespace foodloop {
namespace {

double percent(long long part, long long whole) { return whole == 0 ? 0.0 : 100.0 * part / whole; }

}  // namespace

Metrics computeMetrics(const AllocationPlan& plan, const DeliveryPlan& delivery, const Params& params,
                       double allocationMs) {
    Metrics m;
    m.supplyKg = plan.supplyKg;
    m.demandKg = plan.demandKg;

    // Recomputed from the allocation list rather than trusted from the solver.
    m.allocatedKg = 0;
    m.allocationCostPaise = 0;
    for (const Allocation& a : plan.allocations) {
        m.allocatedKg += a.kg;
        m.allocationCostPaise += a.cost_paise;
    }

    m.demandFulfilledPct = percent(m.allocatedKg, m.demandKg);
    m.unallocatedSurplusKg = m.supplyKg - m.allocatedKg;

    m.deliveredKg = delivery.deliveredKg;
    m.undeliveredKg = delivery.undeliveredKg;
    m.expiryFeasiblePct = percent(m.deliveredKg, m.allocatedKg);

    m.tripCostPaise = delivery.costPaise;
    m.trips = static_cast<int>(delivery.trips.size());
    m.lowLoadTrips = 0;
    double utilisation = 0;
    for (const Trip& t : delivery.trips) {
        utilisation += utilisationPct(t);
        if (utilisationPct(t) < params.low_load_pct) ++m.lowLoadTrips;
    }
    m.avgUtilisationPct = m.trips == 0 ? 0.0 : utilisation / m.trips;
    m.allocationMs = allocationMs;
    return m;
}

}  // namespace foodloop
