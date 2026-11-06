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

Metrics computeMetrics(const AllocationPlan& plan, const FleetPlan& fleet, const Params& params, double allocationMs) {
    Metrics m = computeMetrics(plan, DeliveryPlan(), params, allocationMs);
    m.deliveredKg = fleet.deliveredKg;
    m.undeliveredKg = fleet.undeliveredKg;
    m.expiryFeasiblePct = percent(m.deliveredKg, m.allocatedKg);
    m.tripCostPaise = fleet.costPaise;
    m.trips = static_cast<int>(fleet.trips.size());
    m.km = fleet.km;
    double utilisation = 0;
    for (const FleetTrip& t : fleet.trips) {
        utilisation += utilisationPct(t);
        if (utilisationPct(t) < params.low_load_pct) ++m.lowLoadTrips;
    }
    m.avgUtilisationPct = m.trips == 0 ? 0.0 : utilisation / m.trips;
    for (const VehicleDay& d : fleet.vehicles)
        if (d.trips > 0) ++m.vehiclesUsed;
    m.unmetUnits = (m.supplyKg - m.deliveredKg) + (m.demandKg - m.deliveredKg);
    m.fixedCost = fleet.fixedCostPaise / 100.0;
    m.objective = fleet.costPaise / 100.0 + m.fixedCost + params.unmet_penalty_inr * m.unmetUnits;
    return m;
}

Metrics computeMetrics(const AllocationPlan& plan, const RoutePlan& rp, const Params& params, double allocationMs) {
    Metrics m = computeMetrics(plan, DeliveryPlan(), params, allocationMs);
    m.deliveredKg = rp.deliveredKg;
    m.undeliveredKg = rp.unservedKg;
    m.expiryFeasiblePct = percent(m.deliveredKg, m.allocatedKg);
    m.tripCostPaise = rp.costPaise;
    m.km = rp.km;
    m.vehiclesUsed = rp.vehiclesUsed;
    m.trips = rp.vehiclesUsed;
    double utilisation = 0;
    for (const Route& r : rp.routes) {
        if (r.stops.empty()) continue;
        const double pct = percent(r.maxLoadKg, r.capacityKg);
        utilisation += pct;
        if (pct < params.low_load_pct) ++m.lowLoadTrips;
    }
    m.avgUtilisationPct = m.trips == 0 ? 0.0 : utilisation / m.trips;
    m.unmetUnits = rp.unmetUnits;
    m.fixedCost = rp.fixedCostPaise / 100.0;
    m.objective = rp.objective;
    return m;
}

}  // namespace foodloop
