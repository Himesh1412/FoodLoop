// 0/1 Knapsack by dynamic programming.
#pragma once

#include <vector>

namespace foodloop {

struct KnapsackResult {
    long long value;          // best total value
    int weight;               // total weight of the chosen items
    std::vector<int> chosen;  // indices of the chosen items, ascending
};

// Picks a subset of items with total weight <= capacity and the largest total value.
//   dp[i][w] = best value using the first i items with capacity w
//   dp[i][w] = max(dp[i-1][w], dp[i-1][w - weight_i] + value_i)
// O(n * capacity) time and space (the full table is kept for the traceback).
KnapsackResult knapsack01(const std::vector<int>& weights, const std::vector<long long>& values, int capacity);

}  // namespace foodloop
