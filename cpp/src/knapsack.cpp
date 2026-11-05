#include "knapsack.hpp"

#include <algorithm>
#include <stdexcept>

namespace foodloop {

KnapsackResult knapsack01(const std::vector<int>& weights, const std::vector<long long>& values, int capacity) {
    if (weights.size() != values.size()) throw std::invalid_argument("knapsack01: weights and values differ in size");
    if (capacity < 0) throw std::invalid_argument("knapsack01: negative capacity");
    for (int w : weights)
        if (w < 0) throw std::invalid_argument("knapsack01: negative weight");

    const int n = static_cast<int>(weights.size());
    std::vector<std::vector<long long>> dp(n + 1, std::vector<long long>(capacity + 1, 0));

    for (int i = 1; i <= n; ++i) {
        const int wi = weights[i - 1];
        for (int w = 0; w <= capacity; ++w) {
            dp[i][w] = dp[i - 1][w];                                          // skip item i
            if (wi <= w) dp[i][w] = std::max(dp[i][w], dp[i - 1][w - wi] + values[i - 1]);  // take item i
        }
    }

    // Traceback: if the value changed when item i became available, item i was taken.
    KnapsackResult r;
    r.value = dp[n][capacity];
    r.weight = 0;
    int w = capacity;
    for (int i = n; i >= 1; --i) {
        if (dp[i][w] != dp[i - 1][w]) {
            r.chosen.push_back(i - 1);
            w -= weights[i - 1];
            r.weight += weights[i - 1];
        }
    }
    std::reverse(r.chosen.begin(), r.chosen.end());
    return r;
}

}  // namespace foodloop
