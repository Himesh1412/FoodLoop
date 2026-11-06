# FoodLoop – the ten enhancements (fleet mode)

The reviewer asked for ten enhancements so that FoodLoop can be evaluated on the base paper's own setting
(Dubey & Tanksale, 2023, *Expert Systems with Applications* 232, 120807). All ten are implemented. A dataset
switches to **fleet mode** when it contains `depots.csv`; donor-owned-vehicle datasets (toy, demo, city, large)
behave exactly as before.

| # | Enhancement | How FoodLoop implements it | Code | Tests |
|---|---|---|---|---|
| 1 | Vehicle origins: independent depots and volunteer locations | `depots.csv`; every vehicle starts and ends at its depot (`vehicles.csv: depot_id`) | `data.hpp` (`Depot`, `fleetMode`) | TF.1, TR.1–TR.4 |
| 2 | Dynamic assignment of shared vehicles | Any vehicle can serve any donor and NGO; routing picks the vehicle per request | `routing.cpp` (`insertBest`) | TR.4, TF.6 |
| 3 | Multi-stop routing | Requests (lot → NGO, kg) are inserted by cheapest feasible insertion into multi-stop routes; O(1) time-window check per position | `routing.cpp` | TR.1, TR.8 |
| 4 | Pickup scheduling: food-ready times and collection windows | `packages.csv: ready_hours, pickup_close_hours`; a vehicle waits if early and cannot collect after the window closes | `routing.cpp`, `feasibility.cpp` | TR.3, TF.2–TF.3 |
| 5 | Delivery scheduling: arrival, service and departure times | Every stop records arrival, service start (after waiting for the window) and departure; NGO windows `open_hours, close_hours`; per-stop service time `service_hours`; delivery must finish before expiry | `routing.cpp` (`RouteStop`) | TR.3, TR.8 |
| 6 | Dynamic load tracking | Load after every pickup and delivery (`load_after_kg`), never above capacity, zero on return | `routing.cpp` | TR.1, TR.8 |
| 7 | Split service | A lot can be collected in several visits by several vehicles and delivered to several NGOs; partial insertions re-queue the rest | `routing.cpp` | TR.1, TR.2, TF.7–TF.8 |
| 8 | Objective: on-time delivery first, then actual transport cost | Insertion maximises kg carried, then minimises added distance cost (+ hiring cost for an idle vehicle); plans are compared by the base-paper objective: routing + hiring × vehicles + penalty × unmet units | `routing.cpp`, `metrics.cpp` | TR.4, TR.5 |
| 9 | Joint allocation and routing | Iterative feedback: MCMF allocates → routing → pairs that cannot be routed are removed from the flow network → MCMF re-allocates; leftovers are topped up into the same routes; best plan kept; final ruin-and-recreate improvement | `routing.cpp` (`planJointly`) | TR.6, TR.8 (never worse than one round) |
| 10 | Experimental validation | 180 base-paper instances rebuilt from Cordeau et al. (2001); baselines: Greedy + routing, MCMF without feedback, MCMF with direct trips; comparison with the GA / GA_LS_1 / GA_LS_2 values reported by the base paper | `tools/base_paper.py`, `results/base_paper.md` | — |

## Pipeline in fleet mode

1. Dijkstra distances between depots, donors and NGOs.
2. Feasibility filter: category, reachability, food-ready time + travel + service before expiry, NGO window.
3. Greedy expiry ranking and urgency tiers.
4. **Category-wise Min Cost Max Flow** allocation, tier by tier (exact).
5. **Multi-stop routing** of the allocation by cheapest feasible insertion (three insertion orders).
6. **Feedback** to the allocation for pairs that could not be routed; top-up of leftovers; best plan kept.
7. **Ruin and recreate** improvement of the best plan.
8. Metrics: DF, US, EF, cost, vehicles hired, km, unmet units, base-paper objective.

## Base-paper parameters

`tools/base_paper.py` writes a `params.csv` per instance with `vehicle_fixed_cost_inr = 500` and
`unmet_penalty_inr = 1000` (base paper section 4.1.4.1), `handling_hours = 0` (service times come from the
instance), the depot window as the shift, and crates of 10 units.
