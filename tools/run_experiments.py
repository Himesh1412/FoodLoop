"""Day 6 scenario experiment: MCMF vs Greedy on identical generated instances.

For each scenario, several seeded city-sized datasets are generated into a temporary folder,
the C++ engine is run on each, and both methods' metrics are collected.

Usage (from the project root, after build.bat):
    python tools/run_experiments.py            # 7 scenarios x 5 seeds
    python tools/run_experiments.py --seeds 10

Writes:
    results/day6_scenarios_runs.csv   one row per (scenario, seed, method)
    results/day6_scenarios.md         averages and win counts per scenario
"""
import argparse
import csv
import json
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
from generate_dataset import generate  # noqa: E402

ENGINE = ROOT / "bin" / "foodloop.exe"
PARAMS = ROOT / "config" / "params.csv"
RESULTS = ROOT / "results"

SIZE = dict(rows=6, cols=5, n_donors=12, n_ngos=15)  # same size as data/city

SCENARIOS = [
    ("Baseline", "demand ≈ supply, mixed diets", {}),
    ("Surplus", "demand = 70% of supply", {"demand_ratio": 0.7}),
    ("Shortage", "demand = 140% of supply", {"demand_ratio": 1.4}),
    ("Strict diets", "more vegetarian-only and vegan-only NGOs", {"diet": "strict"}),
    ("Tight expiry", "every shelf life × 0.4", {"expiry_scale": 0.4}),
    ("Small vehicles", "every vehicle capacity × 0.4", {"capacity_scale": 0.4}),
    ("Tight expiry + shortage", "shelf life × 0.4, demand 140%", {"expiry_scale": 0.4, "demand_ratio": 1.4}),
]

METRICS = ["allocated_kg", "demand_fulfilled_pct", "unallocated_surplus_kg", "delivered_kg", "expiry_feasible_pct",
           "allocation_cost_inr", "trip_cost_inr", "trips", "low_load_trips"]


def run_engine(folder):
    out = subprocess.run([str(ENGINE), str(folder), "--json", "--params", str(PARAMS), "--repeat", "1"],
                         capture_output=True, text=True, check=True)
    return json.loads(out.stdout)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--seeds", type=int, default=5)
    a = ap.parse_args()
    if not ENGINE.exists():
        raise SystemExit("bin/foodloop.exe not found: run build.bat first")

    rows = []
    with tempfile.TemporaryDirectory() as tmp:
        for name, _, options in SCENARIOS:
            for seed in range(1, a.seeds + 1):
                folder = Path(tmp) / f"{name}-{seed}"
                generate(folder, seed=seed, quiet=True, **SIZE, **options)
                result = run_engine(folder)
                f = result["feasibility"]
                for method in ("mcmf", "greedy"):
                    m = result["metrics"][method]
                    rows.append({"scenario": name, "seed": seed, "method": method,
                                 "supply_kg": m["supply_kg"], "demand_kg": m["demand_kg"],
                                 "feasible_pairs": f["kept"], "removed_expiry": f["removed_expiry"],
                                 **{k: m[k] for k in METRICS}})
            print(f"  {name}: {a.seeds} instances done")

    RESULTS.mkdir(exist_ok=True)
    with open(RESULTS / "day6_scenarios_runs.csv", "w", newline="", encoding="utf-8") as fh:
        writer = csv.DictWriter(fh, fieldnames=list(rows[0]))
        writer.writeheader()
        writer.writerows(rows)

    def mean(scenario, method, key):
        values = [r[key] for r in rows if r["scenario"] == scenario and r["method"] == method]
        return sum(values) / len(values)

    lines = [
        "# Day 6 – scenario comparison: MCMF vs Greedy",
        "",
        f"Each scenario: {a.seeds} generated instances (seeds 1–{a.seeds}), {SIZE['n_donors']} donors, "
        f"{SIZE['n_ngos']} NGOs, {SIZE['rows'] * SIZE['cols']} junctions. Both methods use the same data, "
        "feasibility rules, trip planner and metrics. Values are averages over the instances.",
        "",
        "| Scenario | Setting | Method | Allocated (kg) | DF % | US (kg) | EF % | TC (₹) | Trip cost (₹) | Trips |",
        "|---|---|---|---|---|---|---|---|---|---|",
    ]
    for name, setting, _ in SCENARIOS:
        for method, label in (("mcmf", "**MCMF**"), ("greedy", "Greedy")):
            lines.append(
                f"| {name if method == 'mcmf' else ''} | {setting if method == 'mcmf' else ''} | {label} "
                f"| {mean(name, method, 'allocated_kg'):.1f} | {mean(name, method, 'demand_fulfilled_pct'):.1f} "
                f"| {mean(name, method, 'unallocated_surplus_kg'):.1f} | {mean(name, method, 'expiry_feasible_pct'):.1f} "
                f"| {mean(name, method, 'allocation_cost_inr'):,.0f} | {mean(name, method, 'trip_cost_inr'):,.0f} "
                f"| {mean(name, method, 'trips'):.1f} |")

    lines += ["", "## Per-instance outcome counts", "",
              "| Scenario | MCMF allocates more | Same kg, MCMF cheaper | Identical | Greedy better | "
              "Avg TC saving | Avg trip-cost saving |",
              "|---|---|---|---|---|---|---|"]
    for name, _, _ in SCENARIOS:
        more = cheaper = same = worse = 0
        tc_saving, trip_saving = [], []
        for seed in range(1, a.seeds + 1):
            m = next(r for r in rows if r["scenario"] == name and r["seed"] == seed and r["method"] == "mcmf")
            g = next(r for r in rows if r["scenario"] == name and r["seed"] == seed and r["method"] == "greedy")
            if m["allocated_kg"] > g["allocated_kg"]:
                more += 1
            elif m["allocated_kg"] < g["allocated_kg"]:
                worse += 1
            elif m["allocation_cost_inr"] < g["allocation_cost_inr"] - 1e-9:
                cheaper += 1
            elif m["allocation_cost_inr"] > g["allocation_cost_inr"] + 1e-9:
                worse += 1
            else:
                same += 1
            if g["allocation_cost_inr"]:
                tc_saving.append(100 * (g["allocation_cost_inr"] - m["allocation_cost_inr"]) / g["allocation_cost_inr"])
            if g["trip_cost_inr"]:
                trip_saving.append(100 * (g["trip_cost_inr"] - m["trip_cost_inr"]) / g["trip_cost_inr"])
        avg = lambda xs: sum(xs) / len(xs) if xs else 0.0
        lines.append(f"| {name} | {more} | {cheaper} | {same} | {worse} | {avg(tc_saving):.1f} % | "
                     f"{avg(trip_saving):.1f} % |")
    lines += ["", "Savings are (Greedy − MCMF) / Greedy; positive means MCMF is cheaper. \"Greedy better\" counts "
              "instances where Greedy allocated more, or the same kg at lower allocation cost.", ""]

    (RESULTS / "day6_scenarios.md").write_text("\n".join(lines), encoding="utf-8")
    print(f"wrote {RESULTS / 'day6_scenarios.md'} and {RESULTS / 'day6_scenarios_runs.csv'}")


if __name__ == "__main__":
    main()
