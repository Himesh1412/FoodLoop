#include "routing.hpp"

#include <algorithm>
#include <cmath>
#include <deque>
#include <limits>
#include <map>
#include <random>
#include <stdexcept>

#include "dijkstra.hpp"
#include "dispatch.hpp"

namespace foodloop {
namespace {

const double EPS = 1e-9;

long long paise(double amount) { return static_cast<long long>(std::floor(amount * 100.0 + 0.5)); }

// Distances between the few locations routing needs: depots, donors and NGOs.
struct Locations {
    std::vector<std::vector<double>> km;  // km[a][b]
    std::vector<int> depotLoc, donorLoc, ngoLoc;

    Locations(const Dataset& data, const Graph& g) {
        const NodeDistances dist(data, g);
        std::vector<std::string> nodes;
        const auto add = [&](const std::string& node) {
            nodes.push_back(node);
            return static_cast<int>(nodes.size()) - 1;
        };
        for (const Depot& d : data.depots) depotLoc.push_back(add(d.node_id));
        for (const Donor& d : data.donors) donorLoc.push_back(add(d.node_id));
        for (const Ngo& n : data.ngos) ngoLoc.push_back(add(n.node_id));
        km.assign(nodes.size(), std::vector<double>(nodes.size(), 0));
        for (size_t a = 0; a < nodes.size(); ++a)
            for (size_t b = 0; b < nodes.size(); ++b) km[a][b] = a == b ? 0 : dist.km(nodes[a], nodes[b]);
    }
};

struct Context {
    const Dataset& data;
    const Params& params;
    const Locations& loc;
    std::vector<Request>& requests;
    std::vector<int> donorOfLot;     // lot -> donor index
    std::vector<int> depotOfVehicle; // vehicle -> depot index

    int locOf(const RouteStop& s) const {
        const Request& r = requests[s.request];
        return s.pickup ? loc.donorLoc[donorOfLot[r.lot]] : loc.ngoLoc[r.ngo];
    }

    // Simulates a route; returns false if any time window, expiry, capacity or shift rule fails.
    // When `fill` is set, writes arrival/start/depart/load into the stops and the route totals.
    bool simulate(int v, std::vector<RouteStop>& stops, Route* fill) const {
        const Vehicle& veh = data.vehicles[v];
        const int home = loc.depotLoc[depotOfVehicle[v]];
        int at = home;
        double clock = 0, km = 0;
        int load = 0, maxLoad = 0, delivered = 0;
        for (RouteStop& s : stops) {
            const Request& r = requests[s.request];
            const Package& lot = data.packages[r.lot];
            const int to = locOf(s);
            const double d = loc.km[at][to];
            if (d == INF) return false;
            km += d;
            const double arrive = clock + d / veh.speed_kmph;
            double start, service;
            if (s.pickup) {
                start = std::max(arrive, lot.ready_hours);
                if (start > lot.pickupClose() + EPS) return false;
                service = data.donors[donorOfLot[r.lot]].pickupService(params.handling_hours);
                load += s.kg;
            } else {
                const Ngo& ngo = data.ngos[r.ngo];
                start = std::max(arrive, ngo.open_hours);
                if (ngo.hasClose() && start > ngo.close_hours + EPS) return false;
                service = ngo.deliveryService(params.handling_hours);
                if (start + service > lot.expiry_hours + EPS) return false;
                load -= s.kg;
                delivered += s.kg;
            }
            if (load > veh.capacity_kg || load < 0) return false;
            maxLoad = std::max(maxLoad, load);
            if (fill) {
                s.arriveHours = arrive;
                s.startHours = start;
                s.departHours = start + service;
                s.loadAfterKg = load;
            }
            clock = start + service;
            at = to;
        }
        if (!stops.empty()) {
            const double d = loc.km[at][home];
            if (d == INF) return false;
            km += d;
            clock += d / veh.speed_kmph;
            if (clock > params.shift_hours + EPS) return false;
        }
        if (fill) {
            fill->km = km;
            fill->costPaise = paise(km * veh.cost_per_km_inr);
            fill->endHours = stops.empty() ? 0 : clock;
            fill->maxLoadKg = maxLoad;
            fill->deliveredKg = delivered;
            fill->capacityKg = veh.capacity_kg;
        }
        return true;
    }

    double routeKm(int v, const std::vector<RouteStop>& stops) const {
        const int home = loc.depotLoc[depotOfVehicle[v]];
        int at = home;
        double km = 0;
        for (const RouteStop& s : stops) {
            km += loc.km[at][locOf(s)];
            at = locOf(s);
        }
        return stops.empty() ? 0 : km + loc.km[at][home];
    }
};

double deadlineOf(const Dataset& data, const Request& r) {
    const Package& lot = data.packages[r.lot];
    double d = std::min(lot.pickupClose(), lot.expiry_hours);
    if (data.ngos[r.ngo].hasClose()) d = std::min(d, data.ngos[r.ngo].close_hours);
    return d;
}

}  // namespace

void scoreRoutePlan(const Dataset& data, const Params& params, RoutePlan& plan) {
    long long supply = 0, demand = 0;
    for (const Package& p : data.packages) supply += p.weight_kg;
    for (const Ngo& n : data.ngos)
        for (const std::string& c : categories()) demand += n.demandFor(c);
    plan.unmetUnits = (supply - plan.deliveredKg) + (demand - plan.deliveredKg);
    plan.objective = plan.costPaise / 100.0 + plan.fixedCostPaise / 100.0 + params.unmet_penalty_inr * plan.unmetUnits;
}

namespace {

// Persistent routes for one planning run: requests can be added and inserted round after round.
class Router {
public:
    Router(const Dataset& data, const Graph& g, const Params& params)
        : data_(data), params_(params), loc_(data, g), ctx_{data, params, loc_, requests_, {}, {}},
          stops_(data.vehicles.size()), km_(data.vehicles.size(), 0), timing_(data.vehicles.size()) {
        if (!data.fleetMode()) throw std::runtime_error("routing needs depots (fleet mode)");
        std::map<std::string, int> donorIndex, depotIndex;
        for (size_t d = 0; d < data.donors.size(); ++d) donorIndex[data.donors[d].id] = static_cast<int>(d);
        for (size_t d = 0; d < data.depots.size(); ++d) depotIndex[data.depots[d].id] = static_cast<int>(d);
        for (const Package& p : data.packages) ctx_.donorOfLot.push_back(donorIndex.at(p.donor_id));
        for (const Vehicle& v : data.vehicles) ctx_.depotOfVehicle.push_back(depotIndex.at(v.depot_id));
    }

    std::vector<Request>& requests() { return requests_; }
    void setTopUpPairs(const Feasibility* feas) { topUpFeas_ = feas; }

    // Adds the allocations as requests and inserts them, most urgent first. Returns the new request ids.
    std::vector<int> addAndInsert(const AllocationPlan& plan, int order_ = 0) {
        std::vector<int> added;
        for (const Allocation& a : plan.allocations) {
            if (a.kg <= 0) continue;
            requests_.push_back(Request{a.lot, a.ngo, a.tier, static_cast<int>(a.kg)});
            added.push_back(static_cast<int>(requests_.size()) - 1);
        }
        std::vector<int> order = added;
        // Insertion order: 0 = earliest deadline, 1 = earliest food-ready time, 2 = largest request.
        const auto key = [&](int r) { return orderKey(r, order_); };
        std::stable_sort(order.begin(), order.end(), [&](int a, int b) {
            const double da = key(a), db = key(b);
            if (std::fabs(da - db) > EPS) return da < db;
            if (requests_[a].tier != requests_[b].tier) return requests_[a].tier < requests_[b].tier;
            return requests_[a].kg > requests_[b].kg;
        });
        std::deque<std::pair<int, int>> queue;  // (request, kg still to route)
        for (int r : order) queue.push_back(std::make_pair(r, requests_[r].kg));
        while (!queue.empty()) {
            const int r = queue.front().first;
            const int want = queue.front().second;
            queue.pop_front();
            const int got = insertBest(r, want);
            if (got > 0 && got < want) queue.push_front(std::make_pair(r, want - got));  // split: rest goes next
        }
        return added;
    }

    // Pair top-up: leftover supply and leftover demand are matched directly by routing, over every
    // feasible (lot, NGO) pair of `feas` (nearest NGO first), until a full pass inserts nothing.
    void pairTopUp(const Feasibility& feas, int passes = 3) {
        for (int pass = 0; pass < passes; ++pass) {
            std::vector<long long> supplyLeft(data_.packages.size());
            std::vector<std::map<std::string, long long>> demandLeft(data_.ngos.size());
            for (size_t k = 0; k < data_.packages.size(); ++k) supplyLeft[k] = data_.packages[k].weight_kg;
            for (size_t j = 0; j < data_.ngos.size(); ++j)
                for (const std::string& c : categories()) demandLeft[j][c] = data_.ngos[j].demandFor(c);
            for (const Request& q : requests_) {
                supplyLeft[q.lot] -= q.servedKg;
                demandLeft[q.ngo][data_.packages[q.lot].category] -= q.servedKg;
            }
            std::vector<int> lots;
            for (size_t k = 0; k < data_.packages.size(); ++k)
                if (supplyLeft[k] > 0) lots.push_back(static_cast<int>(k));
            std::stable_sort(lots.begin(), lots.end(), [&](int a, int b) {
                return data_.packages[a].ready_hours < data_.packages[b].ready_hours;
            });
            bool inserted = false;
            for (int k : lots) {
                std::vector<LotRoute> routes = feas.byLot[k];
                std::stable_sort(routes.begin(), routes.end(),
                                 [](const LotRoute& a, const LotRoute& b) { return a.km < b.km; });
                for (const LotRoute& lr : routes) {
                    if (supplyLeft[k] <= 0) break;
                    long long& need = demandLeft[lr.ngo][data_.packages[k].category];
                    if (need <= 0) continue;
                    const int want = static_cast<int>(std::min(supplyLeft[k], need));
                    const int tier = 0;
                    requests_.push_back(Request{k, lr.ngo, tier, want});
                    const int r = static_cast<int>(requests_.size()) - 1;
                    int got = 0;
                    while (got < want) {
                        const int g = insertBest(r, want - got);
                        if (g == 0) break;
                        got += g;
                    }
                    if (got == 0) {
                        requests_.pop_back();
                        continue;
                    }
                    requests_[r].kg = got;
                    supplyLeft[k] -= got;
                    need -= got;
                    inserted = true;
                }
            }
            if (!inserted) break;
        }
    }

    // Ruin and recreate: remove a few requests from the routes, re-insert them with every unserved
    // request, keep the change only if the base-paper objective improves. Deterministic (fixed seed).
    void improve(int iterations, int order, unsigned seed) {
        std::mt19937 rng(seed);
        double current = quickObjective();
        for (int it = 0; it < iterations; ++it) {
            std::vector<int> routed;
            for (size_t r = 0; r < requests_.size(); ++r)
                if (requests_[r].servedKg > 0) routed.push_back(static_cast<int>(r));
            if (routed.empty()) break;
            const std::vector<std::vector<RouteStop>> savedStops = stops_;
            const std::vector<double> savedKm = km_;
            const std::vector<Request> savedRequests = requests_;

            // Ruin: a random request and its closest neighbours in time (related removal).
            const int seedReq = routed[rng() % routed.size()];
            const double t0 = data_.packages[requests_[seedReq].lot].ready_hours;
            std::sort(routed.begin(), routed.end(), [&](int a, int b) {
                return std::fabs(data_.packages[requests_[a].lot].ready_hours - t0) <
                       std::fabs(data_.packages[requests_[b].lot].ready_hours - t0);
            });
            const size_t k = std::min<size_t>(routed.size(), 2 + rng() % 6);
            std::vector<bool> drop(requests_.size(), false);
            for (size_t i = 0; i < k; ++i) drop[routed[i]] = true;
            for (size_t v = 0; v < stops_.size(); ++v) {
                std::vector<RouteStop> kept;
                for (const RouteStop& st : stops_[v])
                    if (!drop[st.request]) kept.push_back(st);
                stops_[v].swap(kept);
                km_[v] = ctx_.routeKm(static_cast<int>(v), stops_[v]);
            }
            for (size_t r = 0; r < requests_.size(); ++r)
                if (drop[r]) requests_[r].servedKg = 0;
            touchAll();

            // Recreate: every request with unserved kg, in the chosen order with a little noise.
            std::vector<std::pair<double, int>> todo;
            std::uniform_real_distribution<double> noise(0, 60);
            for (size_t r = 0; r < requests_.size(); ++r)
                if (requests_[r].servedKg < requests_[r].kg)
                    todo.push_back(std::make_pair(orderKey(static_cast<int>(r), order) + noise(rng), static_cast<int>(r)));
            std::sort(todo.begin(), todo.end());
            for (const auto& item : todo) {
                int want = requests_[item.second].kg - requests_[item.second].servedKg;
                while (want > 0) {
                    const int got = insertBest(item.second, want);
                    if (got == 0) break;
                    want -= got;
                }
            }
            if (topUpFeas_) pairTopUp(*topUpFeas_, 1);
            const double candidate = quickObjective();
            if (candidate < current - EPS) {
                current = candidate;
            } else {
                stops_ = savedStops;
                km_ = savedKm;
                requests_ = savedRequests;
                touchAll();
            }
        }
    }

    RoutePlan finish(int rounds, int blocked) const {
        RoutePlan rp;
        rp.requests = requests_;
        rp.feedbackRounds = rounds;
        rp.blockedPairs = blocked;
        for (size_t v = 0; v < data_.vehicles.size(); ++v) {
            Route route;
            route.vehicle = static_cast<int>(v);
            route.stops = stops_[v];
            if (!ctx_.simulate(static_cast<int>(v), route.stops, &route))
                throw std::logic_error("routing produced an infeasible route");
            if (!route.stops.empty()) {
                ++rp.vehiclesUsed;
                rp.fixedCostPaise += paise(data_.vehicles[v].hiringCost(params_.vehicle_fixed_cost_inr));
            }
            rp.km += route.km;
            rp.costPaise += route.costPaise;
            rp.deliveredKg += route.deliveredKg;
            rp.routes.push_back(route);
        }
        for (const Request& q : rp.requests) rp.unservedKg += q.kg - q.servedKg;
        // Guard: no lot gives more than it has and no NGO receives more than it asked for.
        std::vector<long long> fromLot(data_.packages.size(), 0);
        std::map<std::pair<int, std::string>, long long> toNgo;
        for (const Request& q : rp.requests) {
            fromLot[q.lot] += q.servedKg;
            toNgo[std::make_pair(q.ngo, data_.packages[q.lot].category)] += q.servedKg;
        }
        for (size_t k = 0; k < fromLot.size(); ++k)
            if (fromLot[k] > data_.packages[k].weight_kg) throw std::logic_error("routing over-used a lot");
        for (const auto& kv : toNgo)
            if (kv.second > data_.ngos[kv.first.first].demandFor(kv.first.second))
                throw std::logic_error("routing over-served an NGO");
        scoreRoutePlan(data_, params_, rp);
        return rp;
    }

private:
    const Feasibility* topUpFeas_ = 0;

    double orderKey(int r, int order) const {
        const Request& q = requests_[r];
        if (order == 1) return data_.packages[q.lot].ready_hours;
        if (order == 2) return -static_cast<double>(q.kg);
        return deadlineOf(data_, q);
    }

    double quickObjective() const {
        long long supply = 0, demand = 0, served = 0;
        for (const Package& p : data_.packages) supply += p.weight_kg;
        for (const Ngo& n : data_.ngos)
            for (const std::string& c : categories()) demand += n.demandFor(c);
        for (const Request& q : requests_) served += q.servedKg;
        double cost = 0;
        for (size_t v = 0; v < stops_.size(); ++v) {
            if (stops_[v].empty()) continue;
            cost += km_[v] * data_.vehicles[v].cost_per_km_inr + data_.vehicles[v].hiringCost(params_.vehicle_fixed_cost_inr);
        }
        return cost + params_.unmet_penalty_inr * ((supply - served) + (demand - served));
    }

    // Supply still free at request r's lot and demand still open at its NGO (in its category).
    int available(int r) const {
        const Request& q = requests_[r];
        const std::string& cat = data_.packages[q.lot].category;
        long long supply = data_.packages[q.lot].weight_kg, demand = data_.ngos[q.ngo].demandFor(cat);
        for (const Request& o : requests_) {
            if (o.lot == q.lot) supply -= o.servedKg;
            if (o.ngo == q.ngo && data_.packages[o.lot].category == cat) demand -= o.servedKg;
        }
        return static_cast<int>(std::max(0LL, std::min(supply, demand)));
    }

    // Time data of one stop: location, window [open, close] for the service start (close already
    // includes the expiry rule for deliveries), and service duration.
    struct StopInfo {
        int loc;
        double open, close, service;
    };
    StopInfo info(int r, bool pickup) const {
        const Request& q = requests_[r];
        const Package& lot = data_.packages[q.lot];
        if (pickup) {
            return StopInfo{loc_.donorLoc[ctx_.donorOfLot[q.lot]], lot.ready_hours, lot.pickupClose(),
                            data_.donors[ctx_.donorOfLot[q.lot]].pickupService(params_.handling_hours)};
        }
        const Ngo& ngo = data_.ngos[q.ngo];
        const double svc = ngo.deliveryService(params_.handling_hours);
        double close = lot.expiry_hours - svc;
        if (ngo.hasClose()) close = std::min(close, ngo.close_hours);
        return StopInfo{loc_.ngoLoc[q.ngo], ngo.open_hours, close, svc};
    }

    // Per-route timing used for O(1) insertion checks: departure time of every stop and the latest
    // service start that keeps every later stop (and the return to the depot) feasible.
    struct Timing {
        bool fresh = false;
        std::vector<double> depart, latest;
        std::vector<int> loadAfter;
        std::vector<StopInfo> stop;
    };
    void refresh(int v) {
        Timing& tm = timing_[v];
        if (tm.fresh) return;
        const std::vector<RouteStop>& route = stops_[v];
        const Vehicle& veh = data_.vehicles[v];
        const int home = loc_.depotLoc[ctx_.depotOfVehicle[v]];
        const int n = static_cast<int>(route.size());
        tm.depart.assign(n, 0);
        tm.latest.assign(n, 0);
        tm.loadAfter.assign(n, 0);
        tm.stop.assign(n, StopInfo{0, 0, 0, 0});
        int at = home, load = 0;
        double clock = 0;
        for (int k = 0; k < n; ++k) {
            const StopInfo si = info(route[k].request, route[k].pickup);
            tm.stop[k] = si;
            clock = std::max(clock + loc_.km[at][si.loc] / veh.speed_kmph, si.open) + si.service;
            tm.depart[k] = clock;
            load += route[k].pickup ? route[k].kg : -route[k].kg;
            tm.loadAfter[k] = load;
            at = si.loc;
        }
        double next = params_.shift_hours;  // latest arrival back at the depot
        int nextLoc = home;
        for (int k = n - 1; k >= 0; --k) {
            const StopInfo& si = tm.stop[k];
            tm.latest[k] = std::min(si.close, next - loc_.km[si.loc][nextLoc] / veh.speed_kmph - si.service);
            next = tm.latest[k];
            nextLoc = si.loc;
        }
        tm.fresh = true;
    }
    void touch(int v) { timing_[v].fresh = false; }
    void touchAll() {
        for (Timing& t : timing_) t.fresh = false;
    }

    // Cheapest feasible insertion of up to `want` kg of request r. Returns the kg inserted (0 = none).
    // For each vehicle and pickup position i the new pickup's times are computed once; delivery
    // positions j = i, i+1, ... are then checked in O(1) each against the stored latest start times.
    int insertBest(int r, int want) {
        want = std::min(want, available(r));  // never more than the lot still has or the NGO still needs
        if (want <= 0) return 0;
        int bestV = -1, bestI = -1, bestJ = -1, bestKg = 0;
        double bestCost = std::numeric_limits<double>::infinity();
        const StopInfo P = info(r, true), D = info(r, false);
        const std::vector<std::vector<double>>& km = loc_.km;
        for (size_t vi = 0; vi < data_.vehicles.size(); ++vi) {
            const int v = static_cast<int>(vi);
            if (stops_[v].empty() && identicalIdleBefore(v)) continue;
            refresh(v);
            const Timing& tm = timing_[v];
            const Vehicle& veh = data_.vehicles[v];
            const double speed = veh.speed_kmph;
            const int home = loc_.depotLoc[ctx_.depotOfVehicle[v]];
            const int n = static_cast<int>(stops_[v].size());
            const double fixed = n == 0 ? veh.hiringCost(params_.vehicle_fixed_cost_inr) : 0.0;
            for (int i = 0; i <= n; ++i) {
                const int prevLoc = i == 0 ? home : tm.stop[i - 1].loc;
                const double prevDep = i == 0 ? 0.0 : tm.depart[i - 1];
                const double sP = std::max(prevDep + km[prevLoc][P.loc] / speed, P.open);
                if (sP > P.close + EPS) continue;
                int segMax = i > 0 ? tm.loadAfter[i - 1] : 0;
                const int afterPrev = i < n ? tm.stop[i].loc : home;
                const double addP = km[prevLoc][P.loc] + km[P.loc][afterPrev] - km[prevLoc][afterPrev];
                int curLoc = P.loc;
                double curDep = sP + P.service;
                for (int j = i; j <= n; ++j) {
                    const int kg = std::min(want, veh.capacity_kg - segMax);
                    if (kg <= 0) break;  // later delivery positions only carry more
                    const double sD = std::max(curDep + km[curLoc][D.loc] / speed, D.open);
                    if (sD <= D.close + EPS && kg >= bestKg) {
                        const int nextLoc = j < n ? tm.stop[j].loc : home;
                        const double arrNext = sD + D.service + km[D.loc][nextLoc] / speed;
                        const bool ok = j < n ? std::max(arrNext, tm.stop[j].open) <= tm.latest[j] + EPS
                                              : arrNext <= params_.shift_hours + EPS;
                        if (ok) {
                            double addKm;
                            if (j == i) {
                                addKm = km[prevLoc][P.loc] + km[P.loc][D.loc] + km[D.loc][afterPrev] -
                                        km[prevLoc][afterPrev];
                            } else {
                                const int before = tm.stop[j - 1].loc;
                                addKm = addP + km[before][D.loc] + km[D.loc][nextLoc] - km[before][nextLoc];
                            }
                            const double cost = addKm * veh.cost_per_km_inr + fixed;
                            if (kg > bestKg || cost < bestCost - EPS) {
                                bestKg = kg;
                                bestCost = cost;
                                bestV = v;
                                bestI = i;
                                bestJ = j;
                            }
                        }
                    }
                    if (j == n) break;
                    // Move stop j in between the new pickup and the new delivery.
                    const double s = std::max(curDep + km[curLoc][tm.stop[j].loc] / speed, tm.stop[j].open);
                    if (s > tm.latest[j] + EPS) break;  // pushed too late: no later delivery position helps
                    curDep = s + tm.stop[j].service;
                    curLoc = tm.stop[j].loc;
                    segMax = std::max(segMax, tm.loadAfter[j]);
                }
            }
        }
        if (bestV < 0) return 0;
        std::vector<RouteStop>& route = stops_[bestV];
        std::vector<RouteStop> next(route.begin(), route.begin() + bestI);
        next.push_back(RouteStop{true, r, bestKg});
        next.insert(next.end(), route.begin() + bestI, route.begin() + bestJ);
        next.push_back(RouteStop{false, r, bestKg});
        next.insert(next.end(), route.begin() + bestJ, route.end());
        route.swap(next);
        km_[bestV] = ctx_.routeKm(bestV, route);
        touch(bestV);
        requests_[r].servedKg += bestKg;
        return bestKg;
    }

    // Idle vehicles at the same depot with the same capacity, speed and cost are interchangeable.
    bool identicalIdleBefore(int v) const {
        const Vehicle& b = data_.vehicles[v];
        for (int u = 0; u < v; ++u) {
            const Vehicle& a = data_.vehicles[u];
            if (stops_[u].empty() && ctx_.depotOfVehicle[u] == ctx_.depotOfVehicle[v] &&
                a.capacity_kg == b.capacity_kg && a.speed_kmph == b.speed_kmph && a.cost_per_km_inr == b.cost_per_km_inr &&
                a.fixed_cost_inr == b.fixed_cost_inr)
                return true;
        }
        return false;
    }

    const Dataset& data_;
    const Params& params_;
    Locations loc_;
    std::vector<Request> requests_;
    Context ctx_;
    std::vector<std::vector<RouteStop>> stops_;
    std::vector<double> km_;
    std::vector<Timing> timing_;
};

}  // namespace

RoutePlan routeAllocation(const Dataset& data, const Graph& g, const AllocationPlan& plan, const Params& params) {
    Router router(data, g, params);
    router.addAndInsert(plan);
    return router.finish(1, 0);
}

namespace {

void blockPair(Feasibility& f, int lot, int ngo) {
    std::vector<LotRoute>& routes = f.byLot[lot];
    for (size_t i = 0; i < routes.size(); ++i)
        if (routes[i].ngo == ngo) {
            routes.erase(routes.begin() + i);
            return;
        }
}

// One restart: allocate with MCMF, route, then top up: re-allocate the still-unserved supply and
// demand over pairs that have not failed and insert the new requests into the same routes.
// Pairs that fail are added to `failed`.
RoutePlan restartRound(const Dataset& data, const Graph& g, const Feasibility& feas, const Params& params,
                       int topUps, int order, AllocationPlan& allocation,
                       std::vector<std::pair<int, int>>& failed, const Feasibility& allPairs,
                       int improveIterations = 0) {
    Router router(data, g, params);
    Feasibility f = feas;
    Dataset residual = data;
    allocation = AllocationPlan();
    for (int t = 0; t <= topUps; ++t) {
        const AllocationPlan plan = allocateByTiers(residual, f, params);
        if (t == 0) allocation = plan;
        else
            for (const Allocation& a : plan.allocations) allocation.allocations.push_back(a);
        long long served = 0;
        bool newFailure = false;
        for (int id : router.addAndInsert(plan, order)) {
            const Request& q = router.requests()[id];
            served += q.servedKg;
            residual.packages[q.lot].weight_kg -= q.servedKg;
            residual.ngos[q.ngo].demand_kg[data.packages[q.lot].category] -= q.servedKg;
            if (q.servedKg < q.kg) {
                failed.push_back(std::make_pair(q.lot, q.ngo));
                blockPair(f, q.lot, q.ngo);
                newFailure = true;
            }
        }
        if (served == 0 && !newFailure) break;
    }
    allocation.allocatedKg = 0;
    allocation.costPaise = 0;
    for (const Allocation& a : allocation.allocations) {
        allocation.allocatedKg += a.kg;
        allocation.costPaise += a.cost_paise;
    }
    router.pairTopUp(allPairs);  // every feasible pair, including those blocked for the allocation
    router.setTopUpPairs(&allPairs);
    if (improveIterations > 0) router.improve(improveIterations, order, 2023);
    return router.finish(1, 0);
}

}  // namespace

RoutePlan planJointly(const Dataset& data, const Graph& g, const Feasibility& feas, const Params& params,
                      AllocationPlan& bestAllocation, int maxRounds, int improveIterations) {
    // Restart rounds: each one re-allocates from scratch on a network from which every pair that
    // failed in earlier rounds has been removed, then routes and tops up (restartRound).
    // The plan with the best base-paper objective is kept.
    // A small portfolio of insertion orders and top-up settings; each runs its own feedback rounds.
    RoutePlan best;
    bool have = false;
    Feasibility bestF = feas;
    int bestOrder = 0, bestTopUps = 0;
    for (int order = 0; order < 3; ++order) {
        for (int topUps : {0, 3}) {
            Feasibility f = feas;
            int blocked = 0;
            std::vector<std::vector<bool>> isBlocked(data.packages.size(), std::vector<bool>(data.ngos.size(), false));
            for (int round = 1; round <= maxRounds; ++round) {
                AllocationPlan allocation;
                std::vector<std::pair<int, int>> failed;
                RoutePlan rp = restartRound(data, g, f, params, topUps, order, allocation, failed, feas);
                rp.feedbackRounds = round;
                rp.blockedPairs = blocked;
                if (!have || rp.objective < best.objective - EPS) {
                    best = rp;
                    bestAllocation = allocation;
                    bestF = f;
                    bestOrder = order;
                    bestTopUps = topUps;
                    have = true;
                }
                bool changed = false;
                for (const std::pair<int, int>& pr : failed) {
                    if (isBlocked[pr.first][pr.second]) continue;
                    isBlocked[pr.first][pr.second] = true;
                    blockPair(f, pr.first, pr.second);
                    ++blocked;
                    changed = true;
                }
                if (!changed) break;
            }
        }
    }
    // Improvement: ruin and recreate on the best configuration found.
    if (have && improveIterations > 0) {
        AllocationPlan allocation;
        std::vector<std::pair<int, int>> failed;
        RoutePlan rp = restartRound(data, g, bestF, params, bestTopUps, bestOrder, allocation, failed, feas,
                                    improveIterations);
        rp.feedbackRounds = best.feedbackRounds;
        rp.blockedPairs = best.blockedPairs;
        if (rp.objective < best.objective - EPS) {
            best = rp;
            bestAllocation = allocation;
        }
    }
    return best;
}

}  // namespace foodloop
