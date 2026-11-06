#include "dispatch.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <set>
#include <stdexcept>

#include "dijkstra.hpp"
#include "knapsack.hpp"

namespace foodloop {
namespace {

const double EPS = 1e-9;

struct VehicleState {
    std::string node;   // current junction
    std::string place;  // depot or NGO id, for the schedule
    double clock = 0;
    int number = 0;
};

// A simulated job: one vehicle serving one (donor, NGO) group.
struct Option {
    int group = -1;
    int vehicle = -1;
    double leave = 0, arrivePickup = 0, startPickup = 0, departPickup = 0;
    double arriveDelivery = 0, startDelivery = 0, departDelivery = 0;
    double emptyKm = 0, loadedKm = 0;
    std::vector<int> crates;
    int loadKg = 0;
    long long costPaise = 0;
    double urgency = 0;  // earliest deadline among the chosen crates
    long long selectPaise = 0;  // costPaise + hiring cost if this is the vehicle's first job
    long long value = 0; // kg x tier weight of the chosen crates
};

long long paise(double km, double rupeesPerKm) {
    return static_cast<long long>(std::floor(km * rupeesPerKm * 100.0 + 0.5));
}

}  // namespace

NodeDistances::NodeDistances(const Dataset& data, const Graph& g) : g_(&g), sourceOf_(g.size(), -1) {
    std::set<std::string> nodes;
    for (const Depot& x : data.depots) nodes.insert(x.node_id);
    for (const Donor& x : data.donors) nodes.insert(x.node_id);
    for (const Ngo& x : data.ngos) nodes.insert(x.node_id);
    for (const std::string& id : nodes) {
        const int s = g.indexOf(id);
        if (s < 0) throw std::runtime_error("unknown junction " + id);
        sourceOf_[s] = static_cast<int>(runs_.size());
        runs_.push_back(dijkstra(g, s).dist);
    }
}

double NodeDistances::km(const std::string& fromNode, const std::string& toNode) const {
    const int s = g_->indexOf(fromNode), t = g_->indexOf(toNode);
    if (s < 0 || t < 0 || sourceOf_[s] < 0) throw std::runtime_error("no distances from junction " + fromNode);
    return runs_[sourceOf_[s]][t];
}

double utilisationPct(const FleetTrip& t) { return t.capacityKg == 0 ? 0.0 : 100.0 * t.loadKg / t.capacityKg; }

FleetPlan dispatchFleet(const Dataset& data, const Graph& g, const AllocationPlan& plan, const Params& params,
                        DispatchPolicy policy, double timeWeightPaisePerHour) {
    if (!data.fleetMode()) throw std::runtime_error("dispatchFleet needs depots (fleet mode)");
    const NodeDistances dist(data, g);

    FleetPlan fp;
    fp.crates = makeCrates(plan, params);

    std::map<std::string, int> donorIndex, depotIndex;
    for (size_t d = 0; d < data.donors.size(); ++d) donorIndex[data.donors[d].id] = static_cast<int>(d);
    for (size_t d = 0; d < data.depots.size(); ++d) depotIndex[data.depots[d].id] = static_cast<int>(d);
    const auto donorOf = [&](int crate) { return donorIndex.at(data.packages[fp.crates[crate].lot].donor_id); };
    const auto lotOf = [&](int crate) -> const Package& { return data.packages[fp.crates[crate].lot]; };

    std::vector<VehicleState> state(data.vehicles.size());
    std::vector<const Depot*> home(data.vehicles.size());
    int maxCapacity = 0;
    for (size_t v = 0; v < data.vehicles.size(); ++v) {
        home[v] = &data.depots[depotIndex.at(data.vehicles[v].depot_id)];
        state[v].node = home[v]->node_id;
        state[v].place = home[v]->id;
        maxCapacity = std::max(maxCapacity, data.vehicles[v].capacity_kg);
    }

    std::vector<int> pending;
    for (size_t c = 0; c < fp.crates.size(); ++c) pending.push_back(static_cast<int>(c));

    // Simulates vehicle v serving the crates in `group` (all same donor and NGO). Returns false if impossible.
    const auto simulate = [&](int v, const std::vector<int>& group, Option& out) {
        const Vehicle& veh = data.vehicles[v];
        const VehicleState& vs = state[v];
        const Donor& donor = data.donors[donorOf(group[0])];
        const Ngo& ngo = data.ngos[fp.crates[group[0]].ngo];
        const double emptyKm = dist.km(vs.node, donor.node_id);
        const double loadedKm = dist.km(donor.node_id, ngo.node_id);
        const double backKm = dist.km(ngo.node_id, home[v]->node_id);
        if (emptyKm == INF || loadedKm == INF || backKm == INF) return false;

        const double arrive = vs.clock + emptyKm / veh.speed_kmph;
        const double pickupSvc = donor.pickupService(params.handling_hours);
        const double deliverySvc = ngo.deliveryService(params.handling_hours);
        std::vector<double> starts;
        for (int c : group) starts.push_back(std::max(arrive, lotOf(c).ready_hours));
        std::sort(starts.begin(), starts.end());
        starts.erase(std::unique(starts.begin(), starts.end()), starts.end());

        for (double s : starts) {
            const double departPickup = s + pickupSvc;
            const double arriveDelivery = departPickup + loadedKm / veh.speed_kmph;
            const double startDelivery = std::max(arriveDelivery, ngo.open_hours);
            const double departDelivery = startDelivery + deliverySvc;
            const double back = departDelivery + backKm / veh.speed_kmph;
            // Later pickups only make these worse, so stop at the first violation.
            if (ngo.hasClose() && startDelivery > ngo.close_hours + EPS) return false;
            if (back > params.shift_hours + EPS) return false;

            std::vector<int> eligible;
            std::vector<int> weights;
            std::vector<long long> values;
            for (int c : group) {
                const Package& p = lotOf(c);
                if (p.ready_hours > s + EPS || s > p.pickupClose() + EPS) continue;  // pickup window
                if (departDelivery > p.expiry_hours + EPS) continue;                   // expiry
                if (fp.crates[c].kg > veh.capacity_kg) continue;
                eligible.push_back(c);
                weights.push_back(fp.crates[c].kg);
                values.push_back(fp.crates[c].value);
            }
            if (eligible.empty()) continue;

            long long total = 0;
            for (int w : weights) total += w;
            KnapsackResult kr;
            if (total <= veh.capacity_kg) {  // everything fits: no DP needed
                kr.weight = static_cast<int>(total);
                kr.value = 0;
                for (size_t i = 0; i < weights.size(); ++i) {
                    kr.chosen.push_back(static_cast<int>(i));
                    kr.value += values[i];
                }
            } else {
                kr = knapsack01(weights, values, veh.capacity_kg);
            }
            out = Option();
            out.value = kr.value;
            out.vehicle = v;
            out.leave = vs.clock;
            out.arrivePickup = arrive;
            out.startPickup = s;
            out.departPickup = departPickup;
            out.arriveDelivery = arriveDelivery;
            out.startDelivery = startDelivery;
            out.departDelivery = departDelivery;
            out.emptyKm = emptyKm;
            out.loadedKm = loadedKm;
            out.loadKg = kr.weight;
            out.costPaise = paise(emptyKm + loadedKm, veh.cost_per_km_inr);
            out.selectPaise = out.costPaise + (vs.number == 0 ? paise(1, veh.hiringCost(params.vehicle_fixed_cost_inr)) : 0);
            out.urgency = std::numeric_limits<double>::infinity();
            for (int i : kr.chosen) {
                out.crates.push_back(eligible[i]);
                const Package& p = lotOf(eligible[i]);
                out.urgency = std::min(out.urgency, std::min(p.pickupClose(), p.expiry_hours));
            }
            return !out.crates.empty();
        }
        return false;
    };

    // a. Group waiting crates by (donor, NGO). Simulated jobs are cached per (group, vehicle) and
    //    recomputed only when that group or that vehicle changes, so each step costs O(groups + vehicles)
    //    simulations instead of O(groups x vehicles).
    struct Cached {
        bool fresh = false;
        bool ok = false;
        Option option;
    };
    std::map<std::pair<int, int>, std::vector<int>> byPair;
    for (int c : pending) byPair[std::make_pair(donorOf(c), fp.crates[c].ngo)].push_back(c);
    std::map<std::pair<int, int>, std::vector<Cached>> cache;
    for (const auto& kv : byPair) cache[kv.first].assign(data.vehicles.size(), Cached());

    while (!byPair.empty()) {
        // b. Simulate every stale (group, vehicle) job.
        std::vector<const Option*> optionPtrs;
        std::vector<std::pair<int, int>> groupKey;
        for (const auto& kv : byPair) {
            std::vector<Cached>& row = cache[kv.first];
            for (size_t v = 0; v < data.vehicles.size(); ++v) {
                if (!row[v].fresh) {
                    row[v].ok = simulate(static_cast<int>(v), kv.second, row[v].option);
                    row[v].fresh = true;
                }
                if (row[v].ok) {
                    row[v].option.group = static_cast<int>(groupKey.size());
                    optionPtrs.push_back(&row[v].option);
                }
            }
            groupKey.push_back(kv.first);
        }
        if (optionPtrs.empty()) break;
        std::vector<Option> options;
        options.reserve(optionPtrs.size());
        for (const Option* p : optionPtrs) options.push_back(*p);

        // THROUGHPUT: most value per hour of vehicle time, over every group.
        const Option* best = 0;
        if (policy == DispatchPolicy::THROUGHPUT) {
            double bestRate = -1;
            for (const Option& o : options) {
                const double rate = o.value / std::max(o.departDelivery - o.leave, 1e-6);
                if (rate > bestRate + EPS || (std::fabs(rate - bestRate) <= EPS && o.selectPaise < best->selectPaise)) {
                    bestRate = rate;
                    best = &o;
                }
            }
        }

        // c. Earliest Deadline First over the schedulable crates.
        double urgent = std::numeric_limits<double>::infinity();
        int group = -1;
        for (const Option& o : options) {
            if (best) break;
            if (o.urgency < urgent - EPS || (std::fabs(o.urgency - urgent) <= EPS && o.group < group)) {
                urgent = o.urgency;
                group = o.group;
            }
        }

        // d. Choose the vehicle for that group.
        for (const Option& o : options) {
            if (policy == DispatchPolicy::THROUGHPUT) break;
            if (o.group != group || o.urgency > urgent + EPS) continue;
            if (!best) {
                best = &o;
                continue;
            }
            bool better;
            if (policy == DispatchPolicy::COST_AWARE) {
                better = o.selectPaise < best->selectPaise ||
                         (o.selectPaise == best->selectPaise && o.departDelivery < best->departDelivery - EPS);
            } else if (policy == DispatchPolicy::BALANCED) {
                const double so = o.selectPaise + timeWeightPaisePerHour * o.departDelivery;
                const double sb = best->selectPaise + timeWeightPaisePerHour * best->departDelivery;
                better = so < sb - EPS;
            } else {
                better = o.arrivePickup < best->arrivePickup - EPS;
            }
            if (better) best = &o;
        }

        // Commit the job.
        const Option o = *best;
        const std::pair<int, int> committedGroup = groupKey[o.group];
        const Vehicle& veh = data.vehicles[o.vehicle];
        VehicleState& vs = state[o.vehicle];
        FleetTrip t;
        t.vehicle = o.vehicle;
        t.number = ++vs.number;
        t.donor = donorOf(o.crates[0]);
        t.ngo = fp.crates[o.crates[0]].ngo;
        t.fromPlace = vs.place;
        t.leaveHours = o.leave;
        t.pickup = Stop{"pickup", data.donors[t.donor].id, o.arrivePickup, o.startPickup, o.departPickup, o.loadKg};
        t.delivery = Stop{"delivery", data.ngos[t.ngo].id, o.arriveDelivery, o.startDelivery, o.departDelivery, 0};
        t.waitHours = (o.startPickup - o.arrivePickup) + (o.startDelivery - o.arriveDelivery);
        t.emptyKm = o.emptyKm;
        t.loadedKm = o.loadedKm;
        t.crates = o.crates;
        t.loadKg = o.loadKg;
        t.capacityKg = veh.capacity_kg;
        t.costPaise = o.costPaise;
        fp.trips.push_back(t);
        fp.deliveredKg += t.loadKg;
        for (int cr : t.crates) fp.deliveredValue += fp.crates[cr].value;
        fp.costPaise += t.costPaise;
        fp.km += t.emptyKm + t.loadedKm;

        vs.node = data.ngos[t.ngo].node_id;
        vs.place = data.ngos[t.ngo].id;
        vs.clock = o.departDelivery;

        std::vector<int> rest;
        for (int c : pending)
            if (std::find(o.crates.begin(), o.crates.end(), c) == o.crates.end()) rest.push_back(c);
        pending.swap(rest);

        // Invalidate the cache for the committed group and the moved vehicle.
        std::vector<int>& left = byPair[committedGroup];
        std::vector<int> keep;
        for (int c : left)
            if (std::find(o.crates.begin(), o.crates.end(), c) == o.crates.end()) keep.push_back(c);
        if (keep.empty()) {
            byPair.erase(committedGroup);
            cache.erase(committedGroup);
        } else {
            left.swap(keep);
            for (Cached& cc : cache[committedGroup]) cc.fresh = false;
        }
        for (auto& kv : cache) kv.second[o.vehicle].fresh = false;
    }

    // Undelivered crates, with the reason seen by the vehicle that could reach the donor first.
    for (int c : pending) {
        const Package& p = lotOf(c);
        const Donor& donor = data.donors[donorOf(c)];
        const Ngo& ngo = data.ngos[fp.crates[c].ngo];
        std::string reason = "unreachable";
        if (fp.crates[c].kg > maxCapacity) {
            reason = "too heavy";
        } else {
            double bestArrive = INF;
            int bestV = -1;
            for (size_t v = 0; v < data.vehicles.size(); ++v) {
                const double km = dist.km(state[v].node, donor.node_id);
                if (km == INF) continue;
                const double arrive = state[v].clock + km / data.vehicles[v].speed_kmph;
                if (arrive < bestArrive) {
                    bestArrive = arrive;
                    bestV = static_cast<int>(v);
                }
            }
            if (bestV >= 0) {
                const Vehicle& veh = data.vehicles[bestV];
                const double s = std::max(bestArrive, p.ready_hours);
                const double pickupSvc = donor.pickupService(params.handling_hours);
                const double deliverySvc = ngo.deliveryService(params.handling_hours);
                const double startDelivery =
                    std::max(s + pickupSvc + dist.km(donor.node_id, ngo.node_id) / veh.speed_kmph, ngo.open_hours);
                const double back =
                    startDelivery + deliverySvc + dist.km(ngo.node_id, home[bestV]->node_id) / veh.speed_kmph;
                if (s > p.pickupClose() + EPS) reason = "pickup window closed";
                else if (startDelivery + deliverySvc > p.expiry_hours + EPS) reason = "expired";
                else if (ngo.hasClose() && startDelivery > ngo.close_hours + EPS) reason = "NGO closed";
                else if (back > params.shift_hours + EPS) reason = "shift over";
                else reason = "no vehicle free";
            }
        }
        fp.undelivered.push_back(Undelivered{c, reason});
        fp.undeliveredKg += fp.crates[c].kg;
    }

    // Return every used vehicle to its depot.
    for (size_t v = 0; v < data.vehicles.size(); ++v) {
        VehicleDay day;
        day.vehicle = static_cast<int>(v);
        for (const FleetTrip& t : fp.trips) {
            if (t.vehicle != static_cast<int>(v)) continue;
            ++day.trips;
            day.km += t.emptyKm + t.loadedKm;
            day.costPaise += t.costPaise;
            day.deliveredKg += t.loadKg;
        }
        if (day.trips > 0) {
            const double back = dist.km(state[v].node, home[v]->node_id);
            day.km += back;
            day.costPaise += paise(back, data.vehicles[v].cost_per_km_inr);
            day.endHours = state[v].clock + back / data.vehicles[v].speed_kmph;
            fp.km += back;
            fp.costPaise += paise(back, data.vehicles[v].cost_per_km_inr);
            ++fp.vehiclesUsed;
            fp.fixedCostPaise += paise(1, data.vehicles[v].hiringCost(params.vehicle_fixed_cost_inr));
        }
        fp.vehicles.push_back(day);
    }
    fp.policy = policy == DispatchPolicy::COST_AWARE ? "cost-aware"
              : policy == DispatchPolicy::BALANCED   ? "balanced"
              : policy == DispatchPolicy::THROUGHPUT ? "throughput"
                                                     : "nearest-vehicle";
    return fp;
}

bool betterFleetPlan(const FleetPlan& a, const FleetPlan& b) {
    if (a.deliveredKg != b.deliveredKg) return a.deliveredKg > b.deliveredKg;
    if (a.deliveredValue != b.deliveredValue) return a.deliveredValue > b.deliveredValue;
    return a.costPaise + a.fixedCostPaise < b.costPaise + b.fixedCostPaise;
}

FleetPlan dispatchBest(const Dataset& data, const Graph& g, const AllocationPlan& plan, const Params& params) {
    // Time weights in paise per hour of later delivery: Rs 100/h and Rs 500/h.
    FleetPlan best = dispatchFleet(data, g, plan, params, DispatchPolicy::COST_AWARE);
    const FleetPlan candidates[] = {
        dispatchFleet(data, g, plan, params, DispatchPolicy::BALANCED, 10000),
        dispatchFleet(data, g, plan, params, DispatchPolicy::BALANCED, 50000),
        dispatchFleet(data, g, plan, params, DispatchPolicy::THROUGHPUT),
        dispatchFleet(data, g, plan, params, DispatchPolicy::NEAREST_VEHICLE),
    };
    for (const FleetPlan& c : candidates)
        if (betterFleetPlan(c, best)) best = c;
    return best;
}

}  // namespace foodloop
