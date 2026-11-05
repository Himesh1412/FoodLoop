#include "trips.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <sstream>
#include <stdexcept>

#include "feasibility.hpp"
#include "knapsack.hpp"

namespace foodloop {
namespace {

const double EPS = 1e-9;  // same tolerance as the Day 3 expiry rule

}  // namespace

std::vector<Crate> makeCrates(const AllocationPlan& plan, const Params& params) {
    std::vector<Crate> crates;
    std::map<int, int> seqOfLot;
    for (const Allocation& a : plan.allocations) {
        for (long long left = a.kg; left > 0;) {
            Crate c;
            c.lot = a.lot;
            c.seq = ++seqOfLot[a.lot];
            c.ngo = a.ngo;
            c.tier = a.tier;
            c.kg = static_cast<int>(std::min<long long>(left, params.crate_kg));
            c.km = a.km;
            c.value = static_cast<long long>(c.kg) * params.tier_weights.at(a.tier);
            crates.push_back(c);
            left -= c.kg;
        }
    }
    return crates;
}

std::string crateLabel(const Dataset& data, const Crate& c) {
    std::ostringstream o;
    o << data.packages[c.lot].id << "#" << c.seq;
    return o.str();
}

double utilisationPct(const Trip& t) { return t.capacityKg == 0 ? 0.0 : 100.0 * t.loadKg / t.capacityKg; }

DeliveryPlan planTrips(const Dataset& data, const AllocationPlan& plan, const Params& params) {
    DeliveryPlan dp;
    dp.crates = makeCrates(plan, params);
    const std::vector<int> vehicleOf = vehicleOfDonor(data);

    std::map<std::string, int> donorIndex;
    for (size_t d = 0; d < data.donors.size(); ++d) donorIndex[data.donors[d].id] = static_cast<int>(d);

    std::vector<std::vector<int>> pendingOf(data.donors.size());
    for (size_t c = 0; c < dp.crates.size(); ++c)
        pendingOf[donorIndex.at(data.packages[dp.crates[c].lot].donor_id)].push_back(static_cast<int>(c));

    for (size_t d = 0; d < data.donors.size(); ++d) {
        const Vehicle& v = data.vehicles[vehicleOf[d]];
        const auto travel = [&](int c) { return dp.crates[c].km / v.speed_kmph; };
        const auto expiry = [&](int c) { return data.packages[dp.crates[c].lot].expiry_hours; };
        const auto drop = [&](int c, const char* reason) {
            Undelivered u = {c, reason};
            dp.undelivered.push_back(u);
            dp.undeliveredKg += dp.crates[c].kg;
        };

        // A crate heavier than the vehicle can never be loaded.
        std::vector<int> pending;
        for (int c : pendingOf[d]) {
            if (dp.crates[c].kg > v.capacity_kg) drop(c, "too heavy");
            else pending.push_back(c);
        }

        double clock = 0.0;
        int number = 0;
        while (!pending.empty()) {
            // 1. Expiry and shift re-check at the current departure time.
            std::vector<int> still;
            for (int c : pending) {
                const double servedBy = clock + travel(c) + params.handling_hours;
                if (servedBy > expiry(c) + EPS) drop(c, "expired");
                else if (servedBy > params.shift_hours + EPS) drop(c, "shift over");
                else still.push_back(c);
            }
            pending.swap(still);
            if (pending.empty()) break;

            // 2. Greedy, Earliest Deadline First: serve the NGO of the crate that must leave soonest.
            int target = -1;
            double tightest = std::numeric_limits<double>::infinity();
            for (int c : pending) {
                const double latestDeparture = expiry(c) - travel(c) - params.handling_hours;
                if (latestDeparture < tightest) {
                    tightest = latestDeparture;
                    target = dp.crates[c].ngo;
                }
            }

            // 3. 0/1 Knapsack over the crates waiting for that NGO.
            std::vector<int> candidates;
            std::vector<int> weights;
            std::vector<long long> values;
            for (int c : pending) {
                if (dp.crates[c].ngo != target) continue;
                candidates.push_back(c);
                weights.push_back(dp.crates[c].kg);
                values.push_back(dp.crates[c].value);
            }
            const KnapsackResult kr = knapsack01(weights, values, v.capacity_kg);
            if (kr.chosen.empty()) throw std::logic_error("knapsack chose no crate although every crate fits");

            // 4. Make the trip.
            Trip t;
            t.donor = static_cast<int>(d);
            t.vehicle = vehicleOf[d];
            t.number = ++number;
            t.ngo = target;
            t.km = dp.crates[candidates[0]].km;
            const double oneWay = t.km / v.speed_kmph;
            t.departHours = clock;
            t.servedByHours = clock + oneWay + params.handling_hours;
            t.returnHours = clock + 2 * oneWay + params.handling_hours;
            t.loadKg = kr.weight;
            t.capacityKg = v.capacity_kg;
            t.costPaise = static_cast<long long>(std::floor(2 * t.km * v.cost_per_km_inr * 100.0 + 0.5));
            for (int i : kr.chosen) t.crates.push_back(candidates[i]);

            std::vector<int> rest;
            for (int c : pending)
                if (std::find(t.crates.begin(), t.crates.end(), c) == t.crates.end()) rest.push_back(c);
            pending.swap(rest);

            dp.deliveredKg += t.loadKg;
            dp.costPaise += t.costPaise;
            dp.trips.push_back(t);
            clock = t.returnHours;
        }
    }
    return dp;
}

}  // namespace foodloop
