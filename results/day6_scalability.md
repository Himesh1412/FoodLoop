# Day 6 – scalability

3 seeded instances per size; NGOs = 1.25 × donors; city area fixed at about 22.5 km × 22.5 km. Times are the engine's own stage timings (ms, steady clock). Values are averages over the seeds.

## Instance size and flow-network size

| Donors | NGOs | Lots L | Junctions | Roads | Feasible pairs | Largest tier V | Largest tier E | Flow F (kg) | Augmenting paths |
|---|---|---|---|---|---|---|---|---|---|
| 5 | 6 | 21 | 25 | 39 | 85 | 28 | 73 | 282 | 30 |
| 10 | 12 | 40 | 36 | 59 | 367 | 55 | 259 | 548 | 63 |
| 20 | 25 | 80 | 81 | 141 | 1645 | 110 | 1016 | 1176 | 139 |
| 40 | 50 | 160 | 144 | 258 | 6419 | 216 | 3697 | 2205 | 274 |
| 80 | 100 | 309 | 289 | 528 | 25436 | 428 | 14439 | 4660 | 569 |
| 160 | 200 | 625 | 576 | 1072 | 101312 | 836 | 54297 | 9347 | 1204 |
| 320 | 400 | 1226 | 1089 | 2056 | 398474 | 1681 | 218632 | 17880 | 2343 |

## Stage times (ms)

| Donors | Lots | Dijkstra | Feasibility | MCMF | Knapsack trips | Greedy allocation | MCMF ÷ Greedy |
|---|---|---|---|---|---|---|---|
| 5 | 21 | 0.012 | 0.008 | 0.070 | 0.034 | 0.025 | 3× |
| 10 | 40 | 0.033 | 0.028 | 0.356 | 0.075 | 0.065 | 5× |
| 20 | 80 | 0.137 | 0.105 | 1.887 | 0.192 | 0.150 | 13× |
| 40 | 160 | 0.570 | 0.342 | 12.158 | 0.398 | 0.417 | 29× |
| 80 | 309 | 2.520 | 1.244 | 86.473 | 0.840 | 1.616 | 53× |
| 160 | 625 | 10.709 | 5.944 | 739.263 | 1.894 | 7.122 | 104× |
| 320 | 1226 | 49.459 | 23.629 | 11823.483 | 4.465 | 32.854 | 360× |

## Growth exponents (log-log least squares over 20–320 donors)

`time ∝ L^k`, where L is the number of lots. k = 1 is linear, k = 2 quadratic.

| Quantity | Exponent k vs lots |
|---|---|
| MCMF allocation time | 3.16 |
| Greedy allocation time | 1.99 |
| Dijkstra distances time | 2.15 |
| Feasibility filter time | 2.00 |
| Knapsack trips time | 1.15 |
| Feasible pairs (≈ E) | 2.01 |
| Flow F | 1.01 |
| Augmenting paths | 1.04 |
| Bellman-Ford bound Σ paths·V·E | 3.98 |

## MCMF vs Greedy at every size

| Donors | Allocated: MCMF / Greedy (kg) | TC: MCMF / Greedy (₹) | TC saving | Trip-cost saving |
|---|---|---|---|---|
| 5 | 282 / 282 | 2,944 / 2,994 | 1.7 % | -1.3 % |
| 10 | 548 / 548 | 3,381 / 3,569 | 5.3 % | 11.5 % |
| 20 | 1176 / 1176 | 4,506 / 4,904 | 8.1 % | 3.1 % |
| 40 | 2205 / 2205 | 6,737 / 7,977 | 15.5 % | 16.9 % |
| 80 | 4660 / 4660 | 10,829 / 12,237 | 11.5 % | 14.3 % |
| 160 | 9347 / 9347 | 15,807 / 18,386 | 14.0 % | 17.0 % |
| 320 | 17880 / 17880 | 20,965 / 25,526 | 17.9 % | 23.6 % |
