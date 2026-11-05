#include "allocation.hpp"

#include <algorithm>

#include "greedy.hpp"
#include "mcmf.hpp"

namespace foodloop {
namespace {

typedef std::vector<std::map<std::string, long long>> DemandTable;

// A lot -> (NGO, category) edge in the tier network, remembered so its flow can be read back.
struct PlacedEdge {
    int edgeId;
    int lot;
    const LotRoute* route;
};

// Empty plan with full supply unplaced; fills `remaining` with the full demand d_jf.
AllocationPlan startPlan(const Dataset& data, DemandTable& remaining) {
    AllocationPlan plan;
    remaining.assign(data.ngos.size(), std::map<std::string, long long>());
    for (size_t j = 0; j < data.ngos.size(); ++j) {
        for (const std::string& cat : categories()) {
            remaining[j][cat] = data.ngos[j].demandFor(cat);
            plan.demandKg += remaining[j][cat];
        }
    }
    plan.unplacedKg.assign(data.packages.size(), 0);
    for (size_t k = 0; k < data.packages.size(); ++k) {
        plan.unplacedKg[k] = data.packages[k].weight_kg;
        plan.supplyKg += data.packages[k].weight_kg;
    }
    return plan;
}

void record(AllocationPlan& plan, DemandTable& remaining, const Dataset& data, int lot, const LotRoute& r, int tier,
            long long kg) {
    Allocation a;
    a.lot = lot;
    a.ngo = r.ngo;
    a.tier = tier;
    a.kg = kg;
    a.km = r.km;
    a.cost_paise = kg * r.cost_paise_per_kg;
    plan.allocations.push_back(a);

    remaining[r.ngo][data.packages[lot].category] -= kg;
    plan.unplacedKg[lot] -= kg;
    plan.allocatedKg += kg;
    plan.costPaise += a.cost_paise;
}

std::vector<int> lotsInPriorityOrder(const Dataset& data) {
    std::map<std::string, int> lotIndex;
    for (size_t k = 0; k < data.packages.size(); ++k) lotIndex[data.packages[k].id] = static_cast<int>(k);
    std::vector<int> order;
    for (const Package& p : expiryPriorityList(data.packages)) order.push_back(lotIndex[p.id]);
    return order;
}

}  // namespace

AllocationPlan allocateByTiers(const Dataset& data, const Feasibility& feas, const Params& params) {
    DemandTable remaining;
    AllocationPlan plan = startPlan(data, remaining);

    // Lots grouped by tier, most urgent first within each tier (Greedy ranking).
    const int tierCount = static_cast<int>(params.tier_boundaries_hours.size()) + 1;
    std::vector<std::vector<int>> lotsInTier(tierCount);
    for (int k : lotsInPriorityOrder(data))
        lotsInTier[urgencyTier(data.packages[k].expiry_hours, params.tier_boundaries_hours)].push_back(k);

    for (int t = 0; t < tierCount; ++t) {
        TierResult tr;
        tr.tier = t;
        tr.lots = lotsInTier[t];
        tr.supplyKg = 0;

        MinCostMaxFlow net;
        const int source = net.addNode();
        const int sink = net.addNode();
        std::map<std::pair<int, std::string>, int> demandNode;  // (NGO, category) -> node
        std::vector<PlacedEdge> placed;
        int edges = 0;

        for (int k : tr.lots) {
            const Package& lot = data.packages[k];
            tr.supplyKg += lot.weight_kg;

            const int lotNode = net.addNode();
            net.addEdge(source, lotNode, lot.weight_kg, 0);
            ++edges;

            for (const LotRoute& r : feas.byLot[k]) {
                const long long left = remaining[r.ngo][lot.category];
                if (left <= 0) continue;  // filled by an earlier tier

                const std::pair<int, std::string> key(r.ngo, lot.category);
                std::map<std::pair<int, std::string>, int>::const_iterator it = demandNode.find(key);
                int node;
                if (it == demandNode.end()) {
                    node = net.addNode();
                    net.addEdge(node, sink, left, 0);
                    ++edges;
                    demandNode[key] = node;
                } else {
                    node = it->second;
                }

                PlacedEdge pe = {net.addEdge(lotNode, node, lot.weight_kg, r.cost_paise_per_kg), k, &r};
                placed.push_back(pe);
                ++edges;
            }
        }

        const MinCostMaxFlow::Result res = net.solve(source, sink);
        tr.flowKg = res.flow;
        tr.costPaise = res.cost;
        tr.augmentations = res.augmentations;
        tr.networkNodes = net.nodeCount();
        tr.networkEdges = edges;

        // Read back x_kj and carry the used demand over to the next tier.
        for (const PlacedEdge& pe : placed) {
            const long long kg = net.flowOn(pe.edgeId);
            if (kg > 0) record(plan, remaining, data, pe.lot, *pe.route, t, kg);
        }
        plan.tiers.push_back(tr);
    }

    plan.unmetKg = remaining;
    return plan;
}

AllocationPlan allocateGreedy(const Dataset& data, const Feasibility& feas, const Params& params) {
    DemandTable remaining;
    AllocationPlan plan = startPlan(data, remaining);

    for (int k : lotsInPriorityOrder(data)) {
        const Package& lot = data.packages[k];
        const int tier = urgencyTier(lot.expiry_hours, params.tier_boundaries_hours);

        // Nearest first; ties broken by NGO order in the dataset.
        std::vector<const LotRoute*> routes;
        for (const LotRoute& r : feas.byLot[k]) routes.push_back(&r);
        std::stable_sort(routes.begin(), routes.end(),
                         [](const LotRoute* a, const LotRoute* b) { return a->km < b->km; });

        for (const LotRoute* r : routes) {
            const long long kg = std::min(plan.unplacedKg[k], remaining[r->ngo][lot.category]);
            if (kg > 0) record(plan, remaining, data, k, *r, tier, kg);
            if (plan.unplacedKg[k] == 0) break;
        }
    }

    plan.unmetKg = remaining;
    return plan;
}

}  // namespace foodloop
