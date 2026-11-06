"""Real-world case study of the base paper: Robin Hood Army (RHA), Lucknow (Dubey & Tanksale, 2023).

Source data: data/lucknow_rha/table_B2.csv, transcribed from Appendix B, Table B-2 of the first author's
PhD thesis (IIT BHU repository, file 11_annexure.pdf): latitude, longitude, service time (minutes) and
demand of 3 volunteer-hub depots, 25 pickup points and 29 delivery sites (slum areas) in Lucknow.
The five demand columns correspond to the paper's scenarios by their totals:
    base: supply 1198 < demand 2585      S-1: supply = demand = 2585     S-2: supply 2651 > demand 2585
    S-3:  supply = demand = 1198         S-4: supply 1198 > demand 985
    S-5:  base demands with fewer vehicles (2 two-wheelers + 2 four-wheelers per depot).
Fleet per depot (paper, Table 4.8 / 4.11): 3 two-wheelers (capacity 50, hiring cost 250) and
13 four-wheelers (capacity 100, hiring cost 500); penalty 1000 per unmet unit; every pickup and delivery
must be served within a 4.5-hour window.

Assumptions where the paper is silent (stated in the results file):
  L1  distances are great-circle (haversine) kilometres between the coordinates, as with geopy;
  L2  travel speed 20 km/h for both vehicle types (urban Lucknow); routing cost = 1 per km;
  L3  service windows [0, 4.5 h] for every pickup and delivery; vehicles may return to their depot
      up to 6 h after the start; cooked food expires after 6 h;
  L4  crates of 10 meals for loading.

Usage (from the project root):
    python tools/lucknow_case.py build              # writes data/lucknow_<scenario>/ for all six scenarios
    python tools/lucknow_case.py run [--engine bin/foodloop.exe]
"""
import argparse
import csv
import json
import math
import subprocess
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / "data" / "lucknow_rha" / "table_B2.csv"
REPORTED = Path(__file__).with_name("lucknow_reported.csv")
SCENARIOS = {"base": "base", "s1": "S1", "s2": "S2", "s3": "S3", "s4": "S4", "s5": "base"}
FLEET = {"two_wheeler": (50, 250), "four_wheeler": (100, 500)}
SPEED_KMPH = 20.0
WINDOW_H = 4.5
SHIFT_H = 6.0


def haversine_km(a, b):
    lat1, lon1, lat2, lon2 = map(math.radians, (a[0], a[1], b[0], b[1]))
    h = math.sin((lat2 - lat1) / 2) ** 2 + math.cos(lat1) * math.cos(lat2) * math.sin((lon2 - lon1) / 2) ** 2
    return 2 * 6371.0088 * math.asin(math.sqrt(h))


def build(scenario):
    rows = list(csv.DictReader(open(SOURCE, encoding="utf-8")))
    col = SCENARIOS[scenario]
    out = ROOT / "data" / f"lucknow_{scenario}"
    out.mkdir(parents=True, exist_ok=True)
    lat0 = sum(float(r["latitude"]) for r in rows) / len(rows)
    lon0 = sum(float(r["longitude"]) for r in rows) / len(rows)
    coords = {}
    for r in rows:
        node = f"L{r['id']}" if r["kind"] != "depot" else r["id"]
        coords[node] = (float(r["latitude"]), float(r["longitude"]))

    def write(name, header, data):
        with open(out / f"{name}.csv", "w", newline="", encoding="utf-8") as f:
            w = csv.writer(f)
            w.writerow(header)
            w.writerows(data)

    # x/y in km around the centre of the points, for the map only; roads carry the real distances.
    write("nodes", ["node_id", "x_km", "y_km", "label"],
          [(n, round(haversine_km((lat0, lon0), (lat0, lo)) * (1 if lo >= lon0 else -1), 3),
            round(haversine_km((lat0, lon0), (la, lon0)) * (1 if la >= lat0 else -1), 3), n)
           for n, (la, lo) in coords.items()])
    names = list(coords)
    write("roads", ["u", "v", "distance_km"],
          [(u, v, round(haversine_km(coords[u], coords[v]), 4)) for i, u in enumerate(names) for v in names[i + 1:]])
    depots = [r for r in rows if r["kind"] == "depot"]
    write("depots", ["depot_id", "name", "type", "node_id"],
          [(r["id"], f"RHA volunteer hub {k}", "volunteer", r["id"]) for k, r in enumerate(depots, 1)])
    per_depot = {"two_wheeler": 2, "four_wheeler": 2} if scenario == "s5" else {"two_wheeler": 3, "four_wheeler": 13}
    vehicles = []
    for r in depots:
        for vtype, count in per_depot.items():
            cap, fixed = FLEET[vtype]
            for k in range(count):
                vehicles.append((f"{r['id']}_{'2W' if vtype == 'two_wheeler' else '4W'}{k + 1}", r["id"], vtype, cap,
                                 SPEED_KMPH, 1, fixed))
    write("vehicles", ["vehicle_id", "depot_id", "vehicle_type", "capacity_kg", "speed_kmph", "cost_per_km_inr",
                       "fixed_cost_inr"], vehicles)
    pick = [r for r in rows if r["kind"] == "pickup" and int(r[col]) > 0]
    deliv = [r for r in rows if r["kind"] == "delivery" and int(r[col]) < 0]
    write("donors", ["donor_id", "name", "type", "node_id", "service_hours"],
          [(f"P{r['id']}", f"RHA pickup point {r['id']}", "pickup", f"L{r['id']}", round(int(r["service_min"]) / 60, 4))
           for r in pick])
    write("packages", ["package_id", "donor_id", "item", "category", "meals", "weight_kg", "expiry_hours",
                       "ready_hours", "pickup_close_hours"],
          [(f"F{r['id']}", f"P{r['id']}", "cooked meals", "VEG", int(r[col]), int(r[col]), SHIFT_H, 0, WINDOW_H)
           for r in pick])
    write("ngos", ["ngo_id", "name", "type", "node_id", "demand_veg_kg", "demand_non_veg_kg", "demand_vegan_kg",
                   "open_hours", "close_hours", "service_hours"],
          [(f"D{r['id']}", f"RHA delivery site {r['id']}", "slum_distribution", f"L{r['id']}", -int(r[col]), 0, 0,
            0, WINDOW_H, round(int(r["service_min"]) / 60, 4)) for r in deliv])
    with open(out / "params.csv", "w", newline="", encoding="utf-8") as f:
        w = csv.writer(f)
        w.writerow(["key", "value"])
        w.writerows([("handling_hours", 0), ("tier_boundaries_hours", "6;24"), ("crate_kg", 10),
                     ("tier_weights", "3;2;1"), ("shift_hours", SHIFT_H), ("low_load_pct", 20),
                     ("rate_inr_per_kg_km", 1), ("vehicle_fixed_cost_inr", 500), ("unmet_penalty_inr", 1000)])
    supply = sum(int(r[col]) for r in pick)
    demand = -sum(int(r[col]) for r in deliv)
    return out, supply, demand


def run(engine):
    reported = {}
    if REPORTED.exists():
        for r in csv.DictReader(open(REPORTED, encoding="utf-8")):
            reported[(r["scenario"], r["algorithm"])] = r
    lines = ["# FoodLoop on the real Robin Hood Army case (Lucknow) – base paper's case study", "",
             "Data: Table B-2 of the base paper's thesis appendix (25 pickup points, 29 delivery sites, 3 volunteer "
             "hubs, real coordinates, service times and demands). Fleet, hiring costs, penalty and 4.5 h windows as in "
             "the paper. Assumptions L1-L4 (see tools/lucknow_case.py): straight-line km, 20 km/h, return by 6 h. "
             "The paper does not state its distance unit, so routing costs are not directly comparable; unmet units "
             "and vehicles hired are.", "",
             "| Scenario | Supply / demand | Method | Objective | Unmet units | Delivered | Two-wheelers | "
             "Four-wheelers | Routing cost | Time (s) |", "|---|---|---|---|---|---|---|---|---|---|"]
    results = []
    for sc in SCENARIOS:
        folder, supply, demand = build(sc)
        start = time.perf_counter()
        out = subprocess.run([str(ROOT / engine), str(folder), "--json", "--params", str(folder / "params.csv"),
                              "--repeat", "1"], capture_output=True, text=True, check=True).stdout
        wall = time.perf_counter() - start
        d = json.loads(out)
        m = d["metrics"]["mcmf"]
        used = {"two_wheeler": 0, "four_wheeler": 0}
        for t in d["delivery"]["trips"]:
            used["two_wheeler" if "2W" in t["vehicle_id"] else "four_wheeler"] += 1
        label = "base" if sc == "base" else sc.upper().replace("S", "S-")
        results.append({"scenario": label, "objective": m["objective"], "unmet": m["unmet_units"],
                        "delivered": m["delivered_kg"], "two": used["two_wheeler"], "four": used["four_wheeler"],
                        "routing": m["trip_cost_inr"], "seconds": round(wall, 2)})
        lines.append(f"| {label} | {supply} / {demand} | **FoodLoop** | {m['objective']:,.0f} | {m['unmet_units']} | "
                     f"{m['delivered_kg']} | {used['two_wheeler']} | {used['four_wheeler']} | "
                     f"{m['trip_cost_inr']:,.0f} km | {wall:.1f} |")
        for key, name in (("mcmf_direct_dispatch", "MCMF + direct trips"), ("greedy", "Greedy + routing")):
            b = d["metrics"][key]
            lines.append(f"|  |  | {name} | {b['objective']:,.0f} | {b['unmet_units']} | {b['delivered_kg']} | "
                         f"– | – | {b['trip_cost_inr']:,.0f} km | – |")
        for alg in ("GA", "GA_LS_1", "GA_LS_2"):
            p = reported.get((label, alg))
            if not p:
                continue
            unmet = round((float(p["objective"]) - float(p["routing"]) - 250 * int(p["two"]) - 500 * int(p["four"])) / 1000)
            lines.append(f"|  |  | {alg} (paper) | {float(p['objective']):,.0f} | {unmet} | {(supply + demand - unmet) // 2} | "
                         f"{p['two']} | {p['four']} | {float(p['routing']):,.0f} (unit not stated) | {p['cpu']} |")
        print(f"{label:5} FoodLoop objective {m['objective']:>12,.0f}  unmet {m['unmet_units']:>5}  "
              f"vehicles {used['two_wheeler']}+{used['four_wheeler']}  {wall:.1f}s")
    (ROOT / "results").mkdir(exist_ok=True)
    (ROOT / "results" / "lucknow_case.md").write_text("\n".join(lines) + "\n", encoding="utf-8")
    with open(ROOT / "results" / "lucknow_case.csv", "w", newline="", encoding="utf-8") as f:
        w = csv.DictWriter(f, fieldnames=list(results[0]))
        w.writeheader()
        w.writerows(results)
    print("wrote results/lucknow_case.md")


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("cmd", choices=["build", "run"])
    ap.add_argument("--engine", default="bin/foodloop.exe")
    a = ap.parse_args()
    if a.cmd == "build":
        for sc in SCENARIOS:
            folder, supply, demand = build(sc)
            print(f"{folder.name}: supply {supply}, demand {demand}")
    else:
        run(a.engine)


if __name__ == "__main__":
    main()
