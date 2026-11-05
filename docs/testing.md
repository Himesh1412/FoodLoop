# FoodLoop – Testing (Day 6)

Run everything with one command from the project folder:

```bash
run_tests.bat
```

It builds the engine, runs all five C++ test suites, and cross-checks every dataset against NetworkX. It prints `ALL TESTS PASSED` and exits with code 0 only if every check passes.

## 1. Test strategy

| Layer | What it proves | Where |
|---|---|---|
| Hand-solved unit tests | Each algorithm gives the exact answer worked out on paper | `test_day2` (Dijkstra, Greedy), `test_day3` (MCMF), `test_day4` (Knapsack, trips) |
| Formula and validation tests | Cost and metric formulas match the methodology; bad input is rejected | `test_eval` |
| Scenario and edge-case tests | Behaviour under surplus, shortage, incompatible food, small vehicles, boundary values | `test_day6` (T6.1–T6.20) |
| Randomised property tests | Plans are always valid; MCMF is never worse than Greedy where theory guarantees it | `test_day6` (T6.21–T6.22), `test_day4` (Knapsack vs brute force) |
| Independent oracle | An independent library gets the same distances, feasible pairs, and tier flow and cost | `verify_with_networkx.py` on toy, demo, city, large |
| Scenario experiment | MCMF vs Greedy on 70 generated instances | `tools/run_experiments.py` → `results/day6_scenarios.md` |

**Latest run:**

| Suite | Checks |
|---|---|
| `test_day2` | 27 |
| `test_day3` | 137 |
| `test_day4` | 230 |
| `test_eval` | 104 |
| `test_day6` | 1,536 |
| **Total** | **2,034, all passed** |

The NetworkX cross-check also passed on all four datasets.

## 2. Day 6 test cases

All scenarios are built in memory with a known answer, using `R = ₹0.50/kg·km`, 0.5 h handling time, tiers 6 h / 24 h, 10 kg crates and an 8 h shift.

### Supply and demand

| ID | Scenario | Input | Expected | Result |
|---|---|---|---|---|
| T6.1 | Surplus | 30 kg VEG, demand 20 | allocated 20, US 10, DF 100%, unmet 0 | ✅ |
| T6.2 | Shortage | 30 kg, demand 50 | allocated 30, US 0, DF 60%, unmet 20 | ✅ |
| T6.3 | Balanced | 30 kg, demand 30 | allocated 30, US 0, DF 100% | ✅ |
| T6.4 | No demand | demand 0 in every category | 0 feasible pairs (incompatible), 0 allocated, 0 trips | ✅ |
| T6.5 | No food | no lots | supply 0, nothing allocated, no crash, DF 0% | ✅ |

### Food compatibility

| ID | Scenario | Input | Expected | Result |
|---|---|---|---|---|
| T6.6 | Nothing compatible | 2 NON_VEG lots, 2 vegan-only NGOs | all 4 pairs removed as incompatible; both methods allocate 0 | ✅ |
| T6.7 | Exact category match | VEGAN lot, NGO with VEG demand only | not allocated: vegan food does not fill VEG demand (model choice, see §5) | ✅ |
| T6.8 | Mixed routing | NON_VEG and VEG lots; a veg-only NGO 1 km away, an all-food NGO 5 km away | non-veg goes only to the all-food NGO; veg goes to the nearer veg-only NGO | ✅ |

### Insufficient vehicle capacity

| ID | Scenario | Input | Expected | Result |
|---|---|---|---|---|
| T6.9 | Crate heavier than vehicle | 5 kg vehicle, 8 kg crate | allocated 8, delivered 0, "too heavy" 8 kg, EF 0% | ✅ |
| T6.10 | Small vehicle, short shelf life | 10 kg vehicle, 30 kg lot, 1.0 h shelf life, 1 km away | 1 trip (served at 0.53 h); trip 2 would be served at 1.1 h, so 20 kg "expired"; EF 33.3% | ✅ |
| T6.11 | Small vehicle, long shelf life | as T6.10, 10 h shelf life | 3 trips; the third leaves at 1.13 h; everything delivered | ✅ |
| T6.12 | Shift ends | as T6.11 with a 1 h shift | 10 kg delivered, 20 kg "shift over" | ✅ |
| – | Capacity is not in the flow network | same data, 5 kg vs 500 kg vehicle | identical allocation | ✅ |

### Edge cases

| ID | Scenario | Input | Expected | Result |
|---|---|---|---|---|
| T6.13 | Expiry boundary | 3 km at 30 km/h + 0.5 h = 0.6 h; lots with 0.6 h and 0.59 h | 0.6 h is allowed (the rule is inclusive); 0.59 h is removed as too late | ✅ |
| T6.14 | Already expired | `expiry_hours = 0` | counted as expired; never allocated by either method | ✅ |
| T6.15 | Unreachable NGO | NGO on a junction with no roads | removed as unreachable; its demand stays unmet | ✅ |
| T6.16 | Same junction | donor and NGO both at A | distance 0, cost ₹0, trip cost ₹0, delivered | ✅ |
| T6.17 | Tier priority | 3 h lot and 30 h lot competing for 10 kg of demand | the 3 h lot gets the demand | ✅ |

### Greedy vs MCMF

| ID | Scenario | Expected | Result |
|---|---|---|---|
| T6.18 | Greedy strands food (toy structure) | MCMF 16 kg, Greedy 8 kg | ✅ |
| T6.19 | Same quantity, different cost | both 20 kg; MCMF ₹10 (20 kg·km), Greedy ₹20 (40 kg·km) | ✅ |
| T6.20 | Determinism | the same input gives an identical plan | ✅ |
| T6.21 | 500 random instances, single tier | MCMF allocates ≥ Greedy; at equal kg MCMF costs ≤ Greedy; both plans always valid | ✅ |
| T6.22 | 500 random instances, with tiers | reported only, because it isn't guaranteed (see §4) | MCMF < Greedy in 0 of 500 |

**T6.21 breakdown (single tier):**
- MCMF allocated more in **1** instance.
- Same kg but MCMF cheaper in **82**.
- Identical in **417**.

## 3. Scenario comparison on generated cities

Source: `results/day6_scenarios.md`. It has 7 scenarios × 10 seeds, each a city-sized instance with 12 donors, 15 NGOs and 30 junctions. Both methods use the same data, rules, trip planner and metrics.

| Scenario | Allocated: MCMF vs Greedy | MCMF cheaper (TC) in | Avg TC saving | Avg trip-cost saving |
|---|---|---|---|---|
| Baseline | equal in 10/10 | 10/10 | 6.0% | 5.7% |
| Surplus (demand 70%) | equal in 10/10 | 10/10 | 13.7% | 11.5% |
| Shortage (demand 140%) | equal in 10/10 | 10/10 | 2.6% | 4.5% |
| Strict diets | equal in 10/10 | 10/10 | 6.1% | 9.5% |
| Tight expiry (× 0.4) | equal in 10/10 | 10/10 | 8.7% | 12.7% |
| Small vehicles (× 0.4) | equal in 10/10 | 10/10 | 6.0% | 5.5% |
| Tight expiry + shortage | equal in 10/10 | 10/10 | 3.6% | 13.6% |

## 4. Findings

1. **Correctness.** Every algorithm matches hand calculations, and an independent library (NetworkX) agrees on every distance, feasible pair, and tier flow and cost across all four datasets.
2. **Where MCMF wins.**
   - **Cost:** on all 70 generated instances MCMF allocated the same amount as Greedy at a lower cost, saving 2.6–13.7% on allocation cost and 4.5–13.6% on trip cost.
   - **Cost grows with choice:** savings are largest with surplus food, where there are more ways to choose.
   - **Quantity:** MCMF delivers more food only when feasibility is tight enough that a nearest-first choice strands food (T6.18, the toy, 1 of 500 random instances). On well-connected cities, Greedy's spill-over to the next nearest NGO rarely strands anything.
3. **Tiers are a trade-off.** With a single tier, MCMF is provably never worse than Greedy (T6.21). With tiers, each tier is optimal but the overall result is not guaranteed. In practice MCMF never allocated less than Greedy (0 of 500 random instances, 0 of 70 scenario instances).
4. **Expiry-feasible delivery (EF) can be lower for MCMF.** In some scenarios (shortage, strict diets) Greedy's EF is higher. MCMF spreads a lot over more NGOs, which means more trips, so crates on later trips more often miss their expiry. This comes from the "vehicles leave at t = 0" assumption in the allocation stage; the trip stage detects it and reports it honestly.
5. **Robustness.**
   - Empty inputs, zero demand, zero distance, isolated NGOs and expired food are handled without errors.
   - Invalid data is rejected with a clear message (`test_eval`).

## 5. Known limitations confirmed by the tests

- **Category match is exact (T6.7).** Vegan food fills only VEGAN demand, even for a vegetarian NGO. Splitting demand by category keeps the flow model simple; a richer substitution model would be future work.
- **Allocation assumes departure at t = 0 (T6.10, finding 4).** Later trips can miss expiry. These crates are reported as undelivered, not hidden.
- **Tiers are not globally optimal (T6.22).** This is accepted so that urgent food gets the first claim on demand.
- **No multi-stop routes.** Small leftover allocations produce low-load trips (they are counted in the metrics).
