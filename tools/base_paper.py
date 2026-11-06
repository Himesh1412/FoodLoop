"""FoodLoop on the base paper's playground (Dubey & Tanksale, 2023, Expert Systems with Applications 232, 120807).

The base paper builds 180 MDVRP-TW-SP-SD instances from the 20 Cordeau et al. (2001) MDVRPTW instances
pr01-pr20 (thesis chapter 4, section 4.1.4.1, Tables 4.2-4.3):
  - every other detail of the Cordeau instance is kept: coordinates, depots, vehicles per depot,
    vehicle capacity Q, customer time windows [a, b] and service times;
  - customer demands are redrawn: pickup demand ~ integer U[alpha*Q, beta*Q], delivery demand the same
    with a negative sign, with (alpha, beta) = (0.1, 1.2) for sub-classes 1-3, (0.3, 0.8) for 4-6,
    (0.8, 1.2) for 7-9;
  - sub-classes 1/4/7: total pickup = total delivery; 2/5/8: pickup > delivery; 3/6/9: pickup < delivery;
  - objective: routing cost + 500 per vehicle hired + 1000 per unit of unmet pickup or delivery.

Assumptions where the chapter is silent (documented in the results file):
  A1  customers with odd ids are pickup nodes, even ids are delivery nodes;
  A2  in the unbalanced sub-classes the totals differ by one average customer demand, round((alpha+beta)/2*Q);
  A3  Euclidean distances; travel time = distance; routing cost = distance; Cordeau's maximum route
      duration is not used (the base model has none), the depot window [0, 1000] bounds every route;
  A4  no separate shelf life: a lot "expires" at the end of the depot window (perishability is
      represented by the time windows, as in the base paper);
  A5  demand is split into crates of 10 units for vehicle loading.

Mapping onto FoodLoop: pickup node -> donor with one food lot (ready = a, pickup close = b, service = tau);
delivery node -> NGO (VEG demand, open = a, close = b, service = tau); depot -> depot with m vehicles of
capacity Q, speed 1, cost 1 per distance unit.

Usage (from the project root):
    python tools/base_paper.py convert --cordeau DIR --instance 1 --subclass 1 --out data/base_paper_p01_1
    python tools/base_paper.py run --cordeau DIR [--instances 1-20] [--subclasses 1-9]
"""
import argparse
import csv
import json
import math
import random
import subprocess
import sys
import tempfile
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
ENGINE = ROOT / "bin" / "foodloop.exe"
RESULTS = ROOT / "results"
REPORTED = Path(__file__).with_name("base_paper_reported.csv")

RANGES = {1: (0.1, 1.2), 2: (0.1, 1.2), 3: (0.1, 1.2), 4: (0.3, 0.8), 5: (0.3, 0.8), 6: (0.3, 0.8),
          7: (0.8, 1.2), 8: (0.8, 1.2), 9: (0.8, 1.2)}
RELATION = {1: 0, 4: 0, 7: 0, 2: 1, 5: 1, 8: 1, 3: -1, 6: -1, 9: -1}  # sign of (pickup - delivery)
PARAMS = {
    "handling_hours": 0, "tier_boundaries_hours": "6;24", "crate_kg": 10, "tier_weights": "3;2;1",
    "shift_hours": 1000, "low_load_pct": 20, "rate_inr_per_kg_km": 1,
    "vehicle_fixed_cost_inr": 500, "unmet_penalty_inr": 1000,
}


def read_cordeau(path):
    """Cordeau MDVRPTW file -> dict with depots, vehicles per depot, capacity, customers."""
    lines = [l.split() for l in Path(path).read_text().splitlines() if l.strip()]
    _, m, n, t = map(int, lines[0][:4])
    duration, capacity = map(float, lines[1][:2])
    customers = []
    for row in lines[1 + t:1 + t + n]:
        customers.append({"id": int(row[0]), "x": float(row[1]), "y": float(row[2]), "service": float(row[3]),
                          "a": float(row[-2]), "b": float(row[-1])})
    depots = [{"id": int(r[0]), "x": float(r[1]), "y": float(r[2]), "a": float(r[-2]), "b": float(r[-1])}
              for r in lines[1 + t + n:1 + t + n + t]]
    return {"m": m, "capacity": int(capacity), "duration": duration, "customers": customers, "depots": depots}


def draw_demands(inst, subclass, seed):
    """Pickup/delivery demands for one sub-class (rules from the base paper plus A1-A2)."""
    rng = random.Random(seed)
    q = inst["capacity"]
    alpha, beta = RANGES[subclass]
    lo, hi = math.ceil(alpha * q), math.floor(beta * q)
    pick = [c["id"] for c in inst["customers"] if c["id"] % 2 == 1]
    deliv = [c["id"] for c in inst["customers"] if c["id"] % 2 == 0]
    demand = {i: rng.randint(lo, hi) for i in pick + deliv}
    target = RELATION[subclass] * round((alpha + beta) / 2 * q)
    diff = sum(demand[i] for i in pick) - sum(demand[i] for i in deliv) - target
    while diff != 0:  # move single nodes inside [lo, hi] until pickup - delivery = target
        if diff > 0:
            i = rng.choice(pick + deliv)
            step = min(diff, demand[i] - lo) if i in pick else min(diff, hi - demand[i])
            demand[i] += -step if i in pick else step
            diff -= step
        else:
            i = rng.choice(pick + deliv)
            step = min(-diff, hi - demand[i]) if i in pick else min(-diff, demand[i] - lo)
            demand[i] += step if i in pick else -step
            diff += step
    return pick, deliv, demand


def convert(cordeau_file, subclass, out, seed):
    inst = read_cordeau(cordeau_file)
    pick, deliv, demand = draw_demands(inst, subclass, seed)
    out = Path(out)
    out.mkdir(parents=True, exist_ok=True)
    cust = {c["id"]: c for c in inst["customers"]}
    points = {f"C{c['id']}": (c["x"], c["y"]) for c in inst["customers"]}
    points.update({f"S{d['id']}": (d["x"], d["y"]) for d in inst["depots"]})
    horizon = max(d["b"] for d in inst["depots"])

    def write(name, header, rows):
        with open(out / f"{name}.csv", "w", newline="", encoding="utf-8") as f:
            w = csv.writer(f)
            w.writerow(header)
            w.writerows(rows)

    write("nodes", ["node_id", "x_km", "y_km", "label"], [(k, x, y, k) for k, (x, y) in points.items()])
    ids = list(points)
    roads = []
    for i, u in enumerate(ids):  # complete Euclidean graph (A3)
        for v in ids[i + 1:]:
            (x1, y1), (x2, y2) = points[u], points[v]
            roads.append((u, v, round(math.hypot(x1 - x2, y1 - y2), 6)))
    write("roads", ["u", "v", "distance_km"], roads)
    write("depots", ["depot_id", "name", "type", "node_id"],
          [(f"S{d['id']}", f"Depot {d['id']}", "depot", f"S{d['id']}") for d in inst["depots"]])
    write("vehicles", ["vehicle_id", "depot_id", "vehicle_type", "capacity_kg", "speed_kmph", "cost_per_km_inr"],
          [(f"V{d['id']}_{k + 1}", f"S{d['id']}", "vehicle", inst["capacity"], 1, 1)
           for d in inst["depots"] for k in range(inst["m"])])
    write("donors", ["donor_id", "name", "type", "node_id", "service_hours"],
          [(f"P{i}", f"Pickup {i}", "pickup", f"C{i}", cust[i]["service"]) for i in pick])
    write("packages", ["package_id", "donor_id", "item", "category", "meals", "weight_kg", "expiry_hours",
                       "ready_hours", "pickup_close_hours"],
          [(f"L{i}", f"P{i}", "servings", "VEG", demand[i], demand[i], horizon, cust[i]["a"], cust[i]["b"])
           for i in pick])
    write("ngos", ["ngo_id", "name", "type", "node_id", "demand_veg_kg", "demand_non_veg_kg", "demand_vegan_kg",
                   "open_hours", "close_hours", "service_hours"],
          [(f"D{i}", f"Delivery {i}", "delivery", f"C{i}", demand[i], 0, 0, cust[i]["a"], cust[i]["b"],
            cust[i]["service"]) for i in deliv])
    params = dict(PARAMS, shift_hours=horizon)
    with open(out / "params.csv", "w", newline="", encoding="utf-8") as f:
        w = csv.writer(f)
        w.writerow(["key", "value"])
        w.writerows(params.items())
    return {"pickup_total": sum(demand[i] for i in pick), "delivery_total": sum(demand[i] for i in deliv),
            "customers": len(inst["customers"]), "depots": len(inst["depots"]), "vehicles_per_depot": inst["m"],
            "capacity": inst["capacity"]}


def parse_range(text):
    lo, _, hi = text.partition("-")
    return list(range(int(lo), int(hi or lo) + 1))


def run_engine(folder):
    start = time.perf_counter()
    out = subprocess.run([str(ENGINE), str(folder), "--json", "--params", str(Path(folder) / "params.csv"),
                          "--repeat", "1"], capture_output=True, text=True, check=True).stdout
    wall = time.perf_counter() - start
    return json.loads(out), wall


def load_reported():
    rows = {}
    if REPORTED.exists():
        for r in csv.DictReader(open(REPORTED, encoding="utf-8")):
            rows[(r["level"], r["key"])] = r
    return rows


def run_all(cordeau, instances, subclasses, seed):
    RESULTS.mkdir(exist_ok=True)
    rows = []
    with tempfile.TemporaryDirectory() as tmp:
        for s in subclasses:
            for i in instances:
                name = f"p{i}_{s}"
                info = convert(Path(cordeau) / f"pr{i:02d}", s, Path(tmp) / name, seed * 1000 + i * 10 + s)
                res, wall = run_engine(Path(tmp) / name)
                row = {"instance": name, "subclass": s, **info, "unavoidable_unmet": abs(
                    info["pickup_total"] - info["delivery_total"]), "wall_s": round(wall, 3),
                       "planner": res.get("planner", "")}
                for key, label in (("mcmf", "foodloop"), ("greedy", "greedy"),
                                   ("mcmf_no_feedback", "nofeedback"), ("mcmf_direct_dispatch", "direct")):
                    m = res["metrics"][key]
                    row.update({f"{label}_objective": m["objective"], f"{label}_routing": m["trip_cost_inr"],
                                f"{label}_vehicles": m["vehicles_used"], f"{label}_unmet": m["unmet_units"],
                                f"{label}_delivered": m["delivered_kg"]})
                row["mcmf_ms"] = res["timings_ms"]["mcmf"]
                row["planning_ms"] = res["timings_ms"]["knapsack_trips"]
                row["feedback_rounds"] = res.get("feedback", {}).get("rounds", "")
                rows.append(row)
                print(f"{name:8} FoodLoop {row['foodloop_objective']:>11.1f}  Greedy {row['greedy_objective']:>11.1f}"
                      f"  direct {row['direct_objective']:>11.1f}  unmet {row['foodloop_unmet']:>5}"
                      f" (unavoidable {row['unavoidable_unmet']})  {wall:.1f}s", flush=True)
    with open(RESULTS / "base_paper_runs.csv", "w", newline="", encoding="utf-8") as f:
        w = csv.DictWriter(f, fieldnames=list(rows[0]))
        w.writeheader()
        w.writerows(rows)
    write_report(rows)


def write_report(rows):
    rep = load_reported()
    lines = ["# FoodLoop on the base paper's instances (Dubey & Tanksale, 2023)", "",
             "Instances rebuilt from Cordeau et al. (2001) pr01-pr20 with the base paper's rules "
             "(see tools/base_paper.py for the rules and assumptions A1-A5). Objective = routing cost + "
             "500 per vehicle hired + 1000 per unit of unmet pickup or delivery (base paper eq. 1). "
             "Base-paper values are copied from the thesis chapter (Tables 4.5 and 4.6); they were obtained "
             "on the authors' own random demand draws, so only the balanced sub-classes (1, 4, 7), where no "
             "penalty is unavoidable, are compared head to head.", ""]
    subs = sorted({r["subclass"] for r in rows})
    lines += ["## Average objective per sub-class", "",
              "| Sub-class | FoodLoop (MCMF + routing + feedback) | MCMF + routing, no feedback | "
              "MCMF + direct trips | Greedy + routing | Unavoidable penalty (ours) | GA best | GA_LS_1 best | "
              "GA_LS_2 best |",
              "|---|---|---|---|---|---|---|---|---|"]
    for s in subs:
        rs = [r for r in rows if r["subclass"] == s]
        avg = lambda k: sum(r[k] for r in rs) / len(rs)  # noqa: E731
        r6 = rep.get(("subclass", str(s)), {})
        lines.append(f"| {s} | {avg('foodloop_objective'):,.0f} | {avg('nofeedback_objective'):,.0f} | "
                     f"{avg('direct_objective'):,.0f} | {avg('greedy_objective'):,.0f} | "
                     f"{1000 * avg('unavoidable_unmet'):,.0f} | "
                     f"{r6.get('ga_best', '–')} | {r6.get('gals1_best', '–')} | {r6.get('gals2_best', '–')} |")
    lines += ["", "## Sub-class 1, instance by instance (balanced: no unavoidable penalty)", "",
              "| Instance | FoodLoop | vehicles | unmet | routing | GA best | GA_LS_1 best | GA_LS_2 best | "
              "FoodLoop time (s) | GA_LS_2 CPU (s) |", "|---|---|---|---|---|---|---|---|---|---|"]
    for r in [r for r in rows if r["subclass"] == 1]:
        p = rep.get(("instance", r["instance"]), {})
        lines.append(f"| {r['instance']} | {r['foodloop_objective']:,.0f} | {r['foodloop_vehicles']} | "
                     f"{r['foodloop_unmet']} | {r['foodloop_routing']:,.0f} | {p.get('ga_best', '–')} | "
                     f"{p.get('gals1_best', '–')} | {p.get('gals2_best', '–')} | {r['wall_s']} | "
                     f"{p.get('gals2_cpu', '–')} |")
    (RESULTS / "base_paper.md").write_text("\n".join(lines) + "\n", encoding="utf-8")
    print("wrote", RESULTS / "base_paper.md")


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)
    c = sub.add_parser("convert")
    c.add_argument("--cordeau", required=True, help="folder with pr01..pr20")
    c.add_argument("--instance", type=int, required=True)
    c.add_argument("--subclass", type=int, required=True)
    c.add_argument("--out", required=True)
    c.add_argument("--seed", type=int, default=2023)
    r = sub.add_parser("run")
    r.add_argument("--cordeau", required=True)
    r.add_argument("--instances", default="1-20")
    r.add_argument("--subclasses", default="1-9")
    r.add_argument("--seed", type=int, default=2023)
    a = ap.parse_args()
    if a.cmd == "convert":
        print(convert(Path(a.cordeau) / f"pr{a.instance:02d}", a.subclass, a.out,
                      a.seed * 1000 + a.instance * 10 + a.subclass))
    else:
        run_all(a.cordeau, parse_range(a.instances), parse_range(a.subclasses), a.seed)


if __name__ == "__main__":
    sys.exit(main())
