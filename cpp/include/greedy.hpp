// Greedy expiry-based prioritisation (Earliest Deadline First) and urgency tiers.
#pragma once

#include <vector>

#include "data.hpp"

namespace foodloop {

// Returns the lots ordered most-urgent first:
//   1. earliest expiry first,
//   2. on equal expiry, more kg first (rescues more food),
//   3. then by lot id, so the order is always the same.
// O(P log P).
std::vector<Package> expiryPriorityList(const std::vector<Package>& packages);

// Urgency tier of a lot, counted from 0. With boundaries {6, 24}:
//   expiry < 6 -> 0,  6 <= expiry < 24 -> 1,  expiry >= 24 -> 2.
int urgencyTier(double expiryHours, const std::vector<double>& boundaries);

}  // namespace foodloop
