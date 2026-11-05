# Day 6 – scenario comparison: MCMF vs Greedy

Each scenario: 10 generated instances (seeds 1–10), 12 donors, 15 NGOs, 30 junctions. Both methods use the same data, feasibility rules, trip planner and metrics. Values are averages over the instances.

| Scenario | Setting | Method | Allocated (kg) | DF % | US (kg) | EF % | TC (₹) | Trip cost (₹) | Trips |
|---|---|---|---|---|---|---|---|---|---|
| Baseline | demand ≈ supply, mixed diets | **MCMF** | 697.0 | 97.9 | 41.2 | 94.0 | 2,256 | 8,130 | 45.2 |
|  |  | Greedy | 697.0 | 97.9 | 41.2 | 93.2 | 2,401 | 8,633 | 46.3 |
| Surplus | demand = 70% of supply | **MCMF** | 500.0 | 100.0 | 238.2 | 87.1 | 1,601 | 5,667 | 37.1 |
|  |  | Greedy | 500.0 | 100.0 | 238.2 | 81.1 | 1,854 | 6,438 | 38.0 |
| Shortage | demand = 140% of supply | **MCMF** | 734.8 | 73.8 | 3.4 | 96.1 | 1,947 | 5,429 | 37.1 |
|  |  | Greedy | 734.8 | 73.8 | 3.4 | 98.5 | 2,005 | 5,701 | 38.5 |
| Strict diets | more vegetarian-only and vegan-only NGOs | **MCMF** | 696.6 | 97.9 | 41.6 | 93.8 | 2,368 | 7,450 | 41.9 |
|  |  | Greedy | 696.6 | 97.9 | 41.6 | 96.1 | 2,522 | 8,233 | 43.4 |
| Tight expiry | every shelf life × 0.4 | **MCMF** | 679.1 | 95.4 | 59.1 | 69.9 | 2,111 | 5,468 | 30.7 |
|  |  | Greedy | 679.1 | 95.4 | 59.1 | 69.5 | 2,312 | 6,297 | 33.1 |
| Small vehicles | every vehicle capacity × 0.4 | **MCMF** | 697.0 | 97.9 | 41.2 | 89.6 | 2,256 | 8,575 | 53.1 |
|  |  | Greedy | 697.0 | 97.9 | 41.2 | 89.2 | 2,401 | 9,075 | 53.8 |
| Tight expiry + shortage | shelf life × 0.4, demand 140% | **MCMF** | 705.7 | 70.8 | 32.5 | 79.3 | 1,825 | 3,831 | 27.9 |
|  |  | Greedy | 705.7 | 70.8 | 32.5 | 83.0 | 1,901 | 4,476 | 30.8 |

## Per-instance outcome counts

| Scenario | MCMF allocates more | Same kg, MCMF cheaper | Identical | Greedy better | Avg TC saving | Avg trip-cost saving |
|---|---|---|---|---|---|---|
| Baseline | 0 | 10 | 0 | 0 | 6.0 % | 5.7 % |
| Surplus | 0 | 10 | 0 | 0 | 13.7 % | 11.5 % |
| Shortage | 0 | 10 | 0 | 0 | 2.6 % | 4.5 % |
| Strict diets | 0 | 10 | 0 | 0 | 6.1 % | 9.5 % |
| Tight expiry | 0 | 10 | 0 | 0 | 8.7 % | 12.7 % |
| Small vehicles | 0 | 10 | 0 | 0 | 6.0 % | 5.5 % |
| Tight expiry + shortage | 0 | 10 | 0 | 0 | 3.6 % | 13.6 % |

Savings are (Greedy − MCMF) / Greedy; positive means MCMF is cheaper. "Greedy better" counts instances where Greedy allocated more, or the same kg at lower allocation cost.
