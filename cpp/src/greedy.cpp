#include "greedy.hpp"

#include <algorithm>

namespace foodloop {
namespace {

bool moreUrgent(const Package& a, const Package& b) {
    if (a.expiry_hours != b.expiry_hours) return a.expiry_hours < b.expiry_hours;
    if (a.weight_kg != b.weight_kg) return a.weight_kg > b.weight_kg;
    return a.id < b.id;
}

}  // namespace

std::vector<Package> expiryPriorityList(const std::vector<Package>& packages) {
    std::vector<Package> ordered(packages);
    std::sort(ordered.begin(), ordered.end(), moreUrgent);
    return ordered;
}

int urgencyTier(double expiryHours, const std::vector<double>& boundaries) {
    int tier = 0;
    while (tier < static_cast<int>(boundaries.size()) && expiryHours >= boundaries[tier]) ++tier;
    return tier;
}

}  // namespace foodloop
