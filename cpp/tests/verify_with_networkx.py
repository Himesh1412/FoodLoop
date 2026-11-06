"""Cross-checks the C++ engine against an independent NetworkX implementation.

Checks:
  1. Dijkstra distance matrix         vs nx.single_source_dijkstra_path_length
  2. Feasible (lot, NGO) pairs + cost vs a re-implementation of the three rules
  3. Each tier's flow and cost         vs nx.max_flow_min_cost on the same tier network

For step 3, the remaining demand entering each tier is taken from the C++ allocations of
the earlier tiers, so every tier is checked for optimality on its own.

Usage (from the project root, after build.bat):
    python cpp/tests/verify_with_networkx.py data/demo
"""
import csv
import json
import subprocess
import sys
from collections import defaultdict
from pathlib import Path

import networkx as nx

CATEGORIES = ["VEG", "NON_VEG", "VEGAN"]


def read_csv(path):
    with open(path, newline="", encoding="utf-8-sig") as f:
        return list(csv.DictReader(f))


def tier_of(expiry, boundaries):
    tier = 0
    while tier < len(boundaries) and expiry >= boundaries[tier]:
        tier += 1
    return tier + 1  # 1-based, as in the JSON


def main(dataset):
    root = Path(__file__).resolve().parents[2]
    data = root / dataset
    out = json.loads(subprocess.run(
        [str(root / "bin" / "foodloop.exe"), str(data), "--json", "--params", str(root / "config" / "params.csv")],
        capture_output=True, text=True, check=True).stdout)

    params = {r["key"]: r["value"] for r in read_csv(root / "config" / "params.csv")}
    handling = float(params["handling_hours"])
    boundaries = [float(b) for b in params["tier_boundaries_hours"].split(";")]
    rate = float(params["rate_inr_per_kg_km"])

    g = nx.Graph()
    g.add_nodes_from(r["node_id"] for r in read_csv(data / "nodes.csv"))
    for r in read_csv(data / "roads.csv"):
        g.add_edge(r["u"], r["v"], weight=float(r["distance_km"]))
    donor_node = {r["donor_id"]: r["node_id"] for r in read_csv(data / "donors.csv")}
    vehicles = read_csv(data / "vehicles.csv")
    fleet = (data / "depots.csv").exists()  # fleet mode: shared vehicles, fastest one bounds feasibility
    fleet_speed = max(float(v["speed_kmph"]) for v in vehicles) if fleet else None
    vehicle = {} if fleet else {r["donor_id"]: r for r in vehicles}
    ngos = read_csv(data / "ngos.csv")
    lots = read_csv(data / "packages.csv")
    demand = {(n["ngo_id"], c): int(n[f"demand_{c.lower()}_kg"]) for n in ngos for c in CATEGORIES}

    failures = 0

    # 1. distances
    dist = {d: nx.single_source_dijkstra_path_length(g, node) for d, node in donor_node.items()}
    m = out["distance_matrix"]
    for d, row in zip(m["donors"], m["km"]):
        for n, km in zip(m["ngos"], row):
            want = dist[d].get(next(x["node_id"] for x in ngos if x["ngo_id"] == n))
            if (km is None) != (want is None) or (km is not None and abs(km - want) > 1e-6):
                failures += 1
                print(f"DISTANCE MISMATCH {d}->{n}: C++ {km}, NetworkX {want}")
    print(f"distances: {len(m['donors']) * len(m['ngos'])} checked")

    # 2. feasibility
    feasible = {}  # (lot, ngo) -> cost in paise per kg
    for lot in lots:
        speed = fleet_speed if fleet else float(vehicle[lot["donor_id"]]["speed_kmph"])
        ready = float(lot.get("ready_hours") or 0)
        expiry = float(lot["expiry_hours"])
        close = float(lot["pickup_close_hours"]) if lot.get("pickup_close_hours") not in (None, "", "-1") else expiry
        for n in ngos:
            if demand[(n["ngo_id"], lot["category"])] <= 0:
                continue
            km = dist[lot["donor_id"]].get(n["node_id"])
            if km is None:
                continue
            start = max(ready + handling / 2 + km / speed, float(n.get("open_hours") or 0))
            if start + handling / 2 > expiry + 1e-9:
                continue
            ngo_close = n.get("close_hours")
            if ready > close + 1e-9 or (ngo_close not in (None, "", "-1") and start > float(ngo_close) + 1e-9):
                continue
            feasible[(lot["package_id"], n["ngo_id"])] = int(km * rate * 100 + 0.5)  # C_ij = D_ij * R
    cpp_feasible = {(b["package_id"], r["ngo_id"]): r["cost_paise_per_kg"]
                    for b in out["feasibility"]["by_lot"] for r in b["routes"]}
    if feasible != cpp_feasible:
        failures += 1
        print("FEASIBILITY MISMATCH")
        print("  only in NetworkX:", sorted(set(feasible.items()) - set(cpp_feasible.items())))
        print("  only in C++:     ", sorted(set(cpp_feasible.items()) - set(feasible.items())))
    print(f"feasibility: {len(feasible)} pairs checked")

    # 3. tier-by-tier optimality
    remaining = dict(demand)
    for tier in out["tiers"]:
        t = tier["tier"]
        tier_lots = [l for l in lots if tier_of(float(l["expiry_hours"]), boundaries) == t]
        net = nx.DiGraph()
        net.add_node("S", demand=0)
        for lot in tier_lots:
            net.add_edge("S", ("lot", lot["package_id"]), capacity=int(lot["weight_kg"]), weight=0)
            for n in ngos:
                key = (lot["package_id"], n["ngo_id"])
                left = remaining[(n["ngo_id"], lot["category"])]
                if key in feasible and left > 0:
                    dn = ("ngo", n["ngo_id"], lot["category"])
                    net.add_edge(("lot", lot["package_id"]), dn, capacity=int(lot["weight_kg"]), weight=feasible[key])
                    net.add_edge(dn, "T", capacity=left, weight=0)
        if "T" in net:
            flow = nx.max_flow_min_cost(net, "S", "T")
            flow_kg = sum(flow["S"].values())
            cost = nx.cost_of_flow(net, flow)
        else:
            flow_kg, cost = 0, 0
        ok = flow_kg == tier["flow_kg"] and cost == tier["cost_paise"]
        failures += 0 if ok else 1
        print(f"tier {t}: C++ {tier['flow_kg']} kg / {tier['cost_paise']} paise, "
              f"NetworkX {flow_kg} kg / {cost} paise  {'OK' if ok else 'MISMATCH'}")

        # carry over using the C++ allocation of this tier
        lot_cat = {l["package_id"]: l["category"] for l in lots}
        for a in out["allocations"]:
            if a["tier"] == t:
                remaining[(a["ngo_id"], lot_cat[a["package_id"]])] -= a["kg"]

    print("ALL CHECKS PASSED" if failures == 0 else f"{failures} CHECK(S) FAILED")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1] if len(sys.argv) > 1 else "data/demo"))
