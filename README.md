# FoodLoop – Intelligent Food Redistribution and Waste Minimization

A Design and Analysis of Algorithms (DAA) project. FoodLoop decides how surplus food from donors (restaurants, supermarkets, hostels, event halls) should be shared among NGOs so that the most food reaches people before it expires, at the lowest transport cost.

## Pipeline

| Step | Algorithm | What it decides |
|---|---|---|
| 1 | Dijkstra (binary heap) | Shortest road distance from every donor to every NGO |
| 2 | Feasibility filter | Which (food lot, NGO) pairs are allowed: matching food category, reachable, arrives before expiry |
| 3 | Greedy (Earliest Deadline First) | Urgency ranking and tiers: < 6 h, 6–24 h, ≥ 24 h |
| 4 | Min Cost Max Flow (successive shortest paths + Bellman-Ford) | How many kg of each lot go to each NGO, tier by tier |
| 5 | 0/1 Knapsack (dynamic programming) | Which crates go on each vehicle trip |
| 6 | Greedy baseline | Nearest-NGO-first allocation, compared with MCMF on the same metrics |

All algorithms are written in C++ (`cpp/`). The Streamlit dashboard (`app/`) handles input and visualisation only.

## Run it (Windows)

Requirements: g++ (MinGW, C++14 or later) and Python 3.10+.

```bat
pip install -r requirements.txt
build.bat        :: compiles the C++ engine to bin\foodloop.exe
run_tests.bat    :: runs the C++ test suites
run_app.bat      :: opens the dashboard at http://localhost:8501
```

Command-line engine: `bin\foodloop.exe data\demo` (add `--json` for machine-readable output).

## Repository layout

| Folder | Contents |
|---|---|
| `cpp/` | Engine source (`include/`, `src/`) and tests (`tests/`, including a NetworkX cross-check) |
| `app/` | Streamlit dashboard and the Python ↔ C++ bridge |
| `data/` | Datasets: `toy`, `demo`, `city`, `large` (synthetic) |
| `config/` | Parameters: handling time, tier limits, crate size, shift length, cost rate R |
| `tools/` | Dataset generator, scenario experiments, scalability study |
| `results/` | Experiment outputs (tables, CSV, charts) |
| `docs/` | Problem formulation, testing, complexity analysis, project summary (.docx) |

## Metrics

DF (demand fulfilment), US (unallocated surplus), EF (expiry-feasible delivery), TC (transport cost = Σ kg × km × R) and ET (execution time), reported for both MCMF and the Greedy baseline.
