"""Day 6 scalability experiment: how each stage's running time grows with instance size.

Instances are generated with tools/generate_dataset.py at increasing size (NGOs = 1.25 x donors)
on a city of fixed area (about 22.5 km x 22.5 km). Keeping the area fixed keeps the share of
expiry-feasible (lot, NGO) pairs roughly constant, so the flow network grows with size instead
of being thinned out by distance.

Usage (from the project root, after build.bat):
    python tools/run_scalability.py                     # 5 ... 320 donors, 3 seeds each
    python tools/run_scalability.py --sizes 5 10 20 --seeds 2

Writes:
    results/day6_scalability_runs.csv    one row per instance
    results/day6_scalability.md          averages, growth exponents, MCMF vs Greedy
    results/day6_scalability_time.png    stage time vs number of lots (log-log)
    results/day6_scalability_cost.png    MCMF allocation-cost saving vs size
"""
import argparse
import csv
import json
import math
import subprocess
import sys
import tempfile
import time
from pathlib import Path

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt  # noqa: E402

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
from generate_dataset import generate  # noqa: E402

ENGINE = ROOT / "bin" / "foodloop.exe"
PARAMS = ROOT / "config" / "params.csv"
RESULTS = ROOT / "results"
CITY_KM = 22.5

# Stages in fixed colour order (validated categorical palette, light mode).
STAGES = [("mcmf", "MCMF allocation", "#2a78d6", "o"),
          ("greedy_allocation", "Greedy allocation", "#eb6834", "s"),
          ("dijkstra", "Dijkstra distances", "#1baf7a", "^"),
          ("feasibility", "Feasibility filter", "#eda100", "D"),
          ("knapsack_trips", "Knapsack trips", "#e87ba4", "v")]
INK, MUTED, GRID, SURFACE = "#1f1f1e", "#6b6a64", "#e4e3df", "#fcfcfb"


def run_instance(folder, donors, seed):
    ngos = round(donors * 1.25)
    side = math.ceil(math.sqrt((donors + ngos) * 1.5))
    generate(folder, rows=side, cols=side, n_donors=donors, n_ngos=ngos, seed=seed,
             spacing=CITY_KM / (side - 1), quiet=True)
    start = time.perf_counter()
    out = subprocess.run([str(ENGINE), str(folder), "--json", "--params", str(PARAMS), "--repeat", "1"],
                         capture_output=True, text=True, check=True)
    wall = time.perf_counter() - start
    j = json.loads(out.stdout)
    tiers = j["tiers"]
    m, g = j["metrics"]["mcmf"], j["metrics"]["greedy"]
    return {
        "donors": donors, "ngos": ngos, "seed": seed, "junctions": j["road_network"]["junctions"],
        "roads": j["road_network"]["roads"], "lots": len(j["priority_list"]),
        "feasible_pairs": j["feasibility"]["kept"],
        "flow_kg": sum(t["flow_kg"] for t in tiers),
        "augmentations": sum(t["augmentations"] for t in tiers),
        "max_tier_nodes": max(t["nodes"] for t in tiers), "max_tier_edges": max(t["edges"] for t in tiers),
        # Bellman-Ford worst-case work: one O(V*E) search per augmenting path, summed over tiers.
        "bf_bound": sum(t["augmentations"] * t["nodes"] * t["edges"] for t in tiers),
        **{f"{k}_ms": j["timings_ms"][k] for k, *_ in STAGES},
        "engine_wall_s": round(wall, 3),
        "mcmf_kg": m["allocated_kg"], "greedy_kg": g["allocated_kg"],
        "mcmf_tc": m["allocation_cost_inr"], "greedy_tc": g["allocation_cost_inr"],
        "mcmf_trip": m["trip_cost_inr"], "greedy_trip": g["trip_cost_inr"],
    }


def slope(xs, ys):
    """Least-squares slope of log(y) against log(x): the empirical growth exponent."""
    lx, ly = [math.log(x) for x in xs], [math.log(y) for y in ys]
    mx, my = sum(lx) / len(lx), sum(ly) / len(ly)
    return sum((a - mx) * (b - my) for a, b in zip(lx, ly)) / sum((a - mx) ** 2 for a in lx)


def style(ax, title, xlabel, ylabel, grid_axis="both"):
    ax.set_facecolor(SURFACE)
    ax.set_axisbelow(True)  # grid behind the data
    ax.set_title(title, loc="left", fontsize=12, color=INK, pad=12)
    ax.set_xlabel(xlabel, color=MUTED)
    ax.set_ylabel(ylabel, color=MUTED)
    ax.grid(True, which="major", axis=grid_axis, color=GRID, linewidth=0.8)
    ax.tick_params(colors=MUTED, labelsize=9)
    for side in ("top", "right"):
        ax.spines[side].set_visible(False)
    for side in ("left", "bottom"):
        ax.spines[side].set_color(GRID)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--sizes", type=int, nargs="+", default=[5, 10, 20, 40, 80, 160, 320], help="donor counts")
    ap.add_argument("--seeds", type=int, default=3)
    ap.add_argument("--plot-only", action="store_true",
                    help="rebuild the report and charts from results/day6_scalability_runs.csv without re-running")
    a = ap.parse_args()

    if a.plot_only:
        with open(RESULTS / "day6_scalability_runs.csv", newline="", encoding="utf-8") as fh:
            rows = [{k: float(v) for k, v in r.items()} for r in csv.DictReader(fh)]
        a.sizes = sorted({int(r["donors"]) for r in rows})
        a.seeds = len(rows) // len(a.sizes)
        write_outputs(rows, a.sizes, a.seeds)
        return
    if not ENGINE.exists():
        raise SystemExit("bin/foodloop.exe not found: run build.bat first")

    rows = []
    with tempfile.TemporaryDirectory() as tmp:
        for donors in a.sizes:
            for seed in range(1, a.seeds + 1):
                rows.append(run_instance(Path(tmp) / f"n{donors}-s{seed}", donors, seed))
            last = [r for r in rows if r["donors"] == donors]
            print(f"  {donors:4} donors: MCMF {sum(r['mcmf_ms'] for r in last) / len(last):10.2f} ms, "
                  f"Greedy {sum(r['greedy_allocation_ms'] for r in last) / len(last):8.2f} ms")

    RESULTS.mkdir(exist_ok=True)
    with open(RESULTS / "day6_scalability_runs.csv", "w", newline="", encoding="utf-8") as fh:
        writer = csv.DictWriter(fh, fieldnames=list(rows[0]))
        writer.writeheader()
        writer.writerows(rows)
    write_outputs(rows, a.sizes, a.seeds)


def write_outputs(rows, sizes, seeds):
    """Writes the markdown report and both charts from the per-instance rows."""
    a = argparse.Namespace(sizes=sizes, seeds=seeds)
    avg = {n: {k: sum(r[k] for r in rows if r["donors"] == n) / a.seeds for k in rows[0] if k != "seed"}
           for n in sizes}
    big = [n for n in sizes if n >= 20]  # tiny instances are dominated by fixed overhead
    lots = [avg[n]["lots"] for n in big]

    # ---------------------------------------------------------------- report
    lines = [
        "# Day 6 – scalability",
        "",
        f"{a.seeds} seeded instances per size; NGOs = 1.25 × donors; city area fixed at about "
        f"{CITY_KM:g} km × {CITY_KM:g} km. Times are the engine's own stage timings (ms, steady clock). "
        "Values are averages over the seeds.",
        "",
        "## Instance size and flow-network size",
        "",
        "| Donors | NGOs | Lots L | Junctions | Roads | Feasible pairs | Largest tier V | Largest tier E | "
        "Flow F (kg) | Augmenting paths |",
        "|---|---|---|---|---|---|---|---|---|---|",
    ]
    for n in sizes:
        v = avg[n]
        lines.append(f"| {n} | {v['ngos']:.0f} | {v['lots']:.0f} | {v['junctions']:.0f} | {v['roads']:.0f} "
                     f"| {v['feasible_pairs']:.0f} | {v['max_tier_nodes']:.0f} | {v['max_tier_edges']:.0f} "
                     f"| {v['flow_kg']:.0f} | {v['augmentations']:.0f} |")

    lines += ["", "## Stage times (ms)", "",
              "| Donors | Lots | Dijkstra | Feasibility | MCMF | Knapsack trips | Greedy allocation | MCMF ÷ Greedy |",
              "|---|---|---|---|---|---|---|---|"]
    for n in sizes:
        v = avg[n]
        lines.append(f"| {n} | {v['lots']:.0f} | {v['dijkstra_ms']:.3f} | {v['feasibility_ms']:.3f} "
                     f"| {v['mcmf_ms']:.3f} | {v['knapsack_trips_ms']:.3f} | {v['greedy_allocation_ms']:.3f} "
                     f"| {v['mcmf_ms'] / v['greedy_allocation_ms']:.0f}× |")

    lines += ["", f"## Growth exponents (log-log least squares over {big[0]}–{big[-1]} donors)", "",
              "`time ∝ L^k`, where L is the number of lots. k = 1 is linear, k = 2 quadratic.", "",
              "| Quantity | Exponent k vs lots |", "|---|---|"]
    for key, label, *_ in STAGES:
        lines.append(f"| {label} time | {slope(lots, [avg[n][key + '_ms'] for n in big]):.2f} |")
    for key, label in (("feasible_pairs", "Feasible pairs (≈ E)"), ("flow_kg", "Flow F"),
                       ("augmentations", "Augmenting paths"), ("bf_bound", "Bellman-Ford bound Σ paths·V·E")):
        lines.append(f"| {label} | {slope(lots, [avg[n][key] for n in big]):.2f} |")

    lines += ["", "## MCMF vs Greedy at every size", "",
              "| Donors | Allocated: MCMF / Greedy (kg) | TC: MCMF / Greedy (₹) | TC saving | Trip-cost saving |",
              "|---|---|---|---|---|"]
    for n in sizes:
        v = avg[n]
        lines.append(f"| {n} | {v['mcmf_kg']:.0f} / {v['greedy_kg']:.0f} | {v['mcmf_tc']:,.0f} / {v['greedy_tc']:,.0f} "
                     f"| {100 * (v['greedy_tc'] - v['mcmf_tc']) / v['greedy_tc']:.1f} % "
                     f"| {100 * (v['greedy_trip'] - v['mcmf_trip']) / v['greedy_trip']:.1f} % |")
    lines.append("")
    (RESULTS / "day6_scalability.md").write_text("\n".join(lines), encoding="utf-8")

    # ---------------------------------------------------------------- charts
    all_lots = [avg[n]["lots"] for n in sizes]
    fig, ax = plt.subplots(figsize=(9, 5.5), facecolor=SURFACE)
    for key, label, color, marker in STAGES:
        ys = [max(avg[n][key + "_ms"], 1e-4) for n in sizes]
        ax.plot(all_lots, ys, color=color, linewidth=2, marker=marker, markersize=6,
                markeredgecolor=SURFACE, markeredgewidth=1.5, label=label)
        if key in ("mcmf", "knapsack_trips"):  # the clustered middle lines rely on the legend
            ax.annotate(f"{label}\n{ys[-1]:,.0f} ms" if ys[-1] >= 10 else f"{label}\n{ys[-1]:.1f} ms",
                        (all_lots[-1], ys[-1]), xytext=(8, 0), textcoords="offset points", va="center",
                        fontsize=9, color=INK)
    ax.set_xscale("log")
    ax.set_yscale("log")
    style(ax, "Running time per stage as the instance grows", "Food lots (log scale)", "Time per run, ms (log scale)")
    ax.legend(frameon=False, fontsize=9, labelcolor=INK, loc="upper left")
    ax.set_xlim(right=all_lots[-1] * 2.2)
    fig.tight_layout()
    fig.savefig(RESULTS / "day6_scalability_time.png", dpi=150)
    plt.close(fig)

    savings = [100 * (avg[n]["greedy_tc"] - avg[n]["mcmf_tc"]) / avg[n]["greedy_tc"] for n in sizes]
    fig, ax = plt.subplots(figsize=(9, 4.5), facecolor=SURFACE)
    xs = range(len(sizes))
    ax.bar(xs, savings, width=0.6, color="#2a78d6", edgecolor=SURFACE, linewidth=2)
    for x, s in zip(xs, savings):
        ax.annotate(f"{s:.1f}%", (x, s), xytext=(0, 4), textcoords="offset points", ha="center", fontsize=9,
                    color=INK)
    ax.set_xticks(list(xs), [f"{n} donors\n{avg[n]['lots']:.0f} lots" for n in sizes])
    style(ax, "MCMF allocation-cost saving over Greedy (same kg allocated)", "", "Saving, % of Greedy cost",
          grid_axis="y")
    ax.set_ylim(0, max(savings) * 1.2)
    fig.tight_layout()
    fig.savefig(RESULTS / "day6_scalability_cost.png", dpi=150)
    plt.close(fig)
    print(f"wrote {RESULTS / 'day6_scalability.md'}, CSV and 2 charts")


if __name__ == "__main__":
    main()
