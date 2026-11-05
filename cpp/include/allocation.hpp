// Stage 4: allocate lots to NGOs.
//
// allocateByTiers - the proposed method: Min Cost Max Flow, one urgency tier at a time.
//   S --(q_k, 0)--> lot k --(q_k, C_ij)--> (NGO j, category f) --(remaining d_jf, 0)--> T
//   Splitting NGO nodes by category makes this the category-wise MCMF: categories never
//   share an edge, so each category is an independent flow problem inside one network.
//   Demand used by a tier is subtracted before the next tier runs.
//
// allocateGreedy - the baseline for comparison, on the same feasibility data.
#pragma once

#include <map>
#include <string>
#include <vector>

#include "data.hpp"
#include "feasibility.hpp"

namespace foodloop {

struct Allocation {
    int lot;   // index into Dataset::packages
    int ngo;   // index into Dataset::ngos
    int tier;  // 0-based urgency tier of the lot
    long long kg;          // x_kj
    double km;             // D_ij
    long long cost_paise;  // x_kj * C_ij
};

struct TierResult {
    int tier;
    std::vector<int> lots;  // lots in this tier, most urgent first
    long long supplyKg;
    long long flowKg;
    long long costPaise;
    int augmentations;
    int networkNodes;
    int networkEdges;
};

struct AllocationPlan {
    std::vector<Allocation> allocations;
    std::vector<TierResult> tiers;                           // empty for the Greedy baseline
    std::vector<long long> unplacedKg;                       // per lot: supply not allocated
    std::vector<std::map<std::string, long long>> unmetKg;   // per NGO, per category

    long long supplyKg = 0;
    long long demandKg = 0;
    long long allocatedKg = 0;
    long long costPaise = 0;  // TC = sum x_kj * C_ij
};

AllocationPlan allocateByTiers(const Dataset& data, const Feasibility& feas, const Params& params);

// Greedy baseline: lots in expiry order (most urgent first); each lot goes to its nearest
// feasible NGO with unmet demand in its category, spilling over to the next nearest while
// the lot still has food. Never revisits a decision.
AllocationPlan allocateGreedy(const Dataset& data, const Feasibility& feas, const Params& params);

}  // namespace foodloop
