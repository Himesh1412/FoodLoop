"""Generates a synthetic FoodLoop dataset with a fixed random seed (same seed -> same files).

Examples (from the project root):
    python tools/generate_dataset.py --name city  --rows 6  --cols 5  --donors 12 --ngos 15 --seed 7
    python tools/generate_dataset.py --name large --rows 10 --cols 10 --donors 40 --ngos 50 --seed 11

What it builds, with the same columns as data/demo:
  - a road grid (spacing about 2.5 km) with jittered junctions, a few diagonal roads and
    some missing roads; road length is always >= straight-line distance and the network
    stays connected;
  - donors of four types, each with one vehicle and type-specific food lots
    (restaurants and hostels: short shelf life; supermarkets: long shelf life);
  - NGOs with dietary profiles (all food / vegetarian / vegan only) and per-category
    demand set to roughly match supply, with random shortages and surpluses per category.
"""
import argparse
import csv
import math
import random
from pathlib import Path

import networkx as nx

ROOT = Path(__file__).resolve().parents[1]

DONOR_TYPES = {
    # type: (name suffix, vehicle type, capacity kg, speed km/h, Rs per km, lots per donor, kg range, expiry range h)
    "restaurant": ("Restaurant", "scooter", 25, 25, 6, (3, 5), (6, 14), (1.5, 6)),
    "supermarket": ("Supermarket", "van", 120, 30, 15, (3, 6), (6, 20), (6, 48)),
    "hostel": ("Hostel", "auto_rickshaw", 60, 20, 10, (2, 4), (10, 25), (0.75, 6)),
    "event_organizer": ("Banquet Hall", "mini_truck", 80, 30, 12, (3, 5), (10, 35), (2, 10)),
}

ITEMS = {
    "restaurant": [("Veg biryani", "VEG"), ("Dal rice", "VEG"), ("Paneer curry with rice", "VEG"),
                   ("Chicken curry with rice", "NON_VEG"), ("Fish curry meals", "NON_VEG"),
                   ("Vegetable pulao", "VEG"), ("Chole rice", "VEGAN")],
    "supermarket": [("Bread and buns", "VEGAN"), ("Fruit packs", "VEGAN"), ("Curd rice packs", "VEG"),
                    ("Chicken sandwiches", "NON_VEG"), ("Vegetable packs", "VEGAN"), ("Milk and paneer", "VEG")],
    "hostel": [("Chapati and sabzi", "VEG"), ("Khichdi", "VEG"), ("Egg curry with rice", "NON_VEG"),
               ("Sambar rice", "VEGAN"), ("Lemon rice", "VEGAN")],
    "event_organizer": [("Paneer pulao", "VEG"), ("Mutton biryani", "NON_VEG"), ("Veg fried rice", "VEG"),
                        ("Mixed veg salad", "VEGAN"), ("Chicken biryani", "NON_VEG"), ("Gulab jamun", "VEG")],
}

DONOR_PREFIXES = ["Spice Garden", "Green Leaf", "Royal", "Sunrise", "Lotus", "Saffron", "Golden Fork", "Mint",
                  "Tandoor House", "City", "Fresh Basket", "Silver Spoon", "Coconut Grove", "Blue Moon", "Riverside",
                  "Heritage", "Banyan", "Peacock", "Lakeview", "Maple", "Orchid", "Harvest", "Sapphire", "Tulsi",
                  "Jasmine", "Evergreen", "Moonlight", "Sagar", "Nilgiri", "Ganga"]

NGO_PREFIXES = ["Hope", "Asha", "Seva", "Annapurna", "Sneha", "Karuna", "Prerana", "Ujjwal", "Navjeevan",
                "Sahyog", "Shanti", "Aashray", "Disha", "Umeed", "Ankur", "Kiran", "Sparsh", "Aadhar", "Mamta",
                "Jyoti", "Sankalp", "Samarth", "Udaan", "Nanhe Kadam", "Ekta"]
NGO_TYPES = {"orphanage": "Orphanage", "shelter": "Night Shelter", "food_bank": "Food Bank",
             "community_kitchen": "Community Kitchen", "old_age_home": "Old Age Home"}

# Dietary profile -> accepted categories, with probabilities.
PROFILES = [(["VEG", "NON_VEG", "VEGAN"], 0.6), (["VEG", "VEGAN"], 0.25), (["VEGAN"], 0.15)]
STRICT_PROFILES = [(["VEG", "NON_VEG", "VEGAN"], 0.25), (["VEG", "VEGAN"], 0.4), (["VEGAN"], 0.35)]
CATEGORIES = ["VEG", "NON_VEG", "VEGAN"]


def unique_names(rng, prefixes, suffixes, count):
    """count distinct "<prefix> <suffix>" names; suffixes[i] belongs to entity i."""
    used, names = set(), []
    for suffix in suffixes[:count]:
        for attempt in range(100):
            name = f"{rng.choice(prefixes)} {suffix}"
            if attempt >= 50:
                name += f" {attempt - 48}"
            if name not in used:
                break
        used.add(name)
        names.append(name)
    return names


def build_roads(rng, rows, cols, spacing):
    nodes = {}
    for r in range(rows):
        for c in range(cols):
            jitter = lambda: round(rng.uniform(-0.15, 0.15) * spacing, 1)
            nodes[f"J{r * cols + c + 1}"] = (round(c * spacing + jitter(), 1), round(r * spacing + jitter(), 1))
    ids = list(nodes)
    at = lambda r, c: ids[r * cols + c]

    pairs = set()
    for r in range(rows):
        for c in range(cols):
            if c + 1 < cols:
                pairs.add((at(r, c), at(r, c + 1)))
            if r + 1 < rows:
                pairs.add((at(r, c), at(r + 1, c)))
    for _ in range(max(1, rows * cols // 6)):  # a few diagonal roads
        r, c = rng.randrange(rows - 1), rng.randrange(cols)
        c2 = c + rng.choice([-1, 1])
        if 0 <= c2 < cols:
            pairs.add((at(r, c), at(r + 1, c2)))

    # Sorted everywhere: set order of strings changes between Python runs, which would break the seed.
    number = lambda node: int(node[1:])
    pairs = sorted(tuple(sorted(p, key=number)) for p in pairs)
    g = nx.Graph(pairs)
    for u, v in rng.sample(pairs, len(pairs) // 10):  # remove ~10% of roads, staying connected
        g.remove_edge(u, v)
        if not nx.is_connected(g):
            g.add_edge(u, v)

    roads = []
    for u, v in sorted((tuple(sorted(e, key=number)) for e in g.edges), key=lambda e: (number(e[0]), number(e[1]))):
        straight = math.dist(nodes[u], nodes[v])
        roads.append((u, v, math.ceil(straight * rng.uniform(1.05, 1.25) * 10) / 10))  # winding road >= straight
    return nodes, roads


def generate(out, rows, cols, n_donors, n_ngos, seed, spacing=2.5, demand_ratio=1.0, expiry_scale=1.0,
             capacity_scale=1.0, diet="mixed", quiet=False):
    """Writes one dataset to folder `out`. The default options reproduce data/city and data/large."""
    rng = random.Random(seed)
    if n_donors + n_ngos > rows * cols:
        raise SystemExit(f"{n_donors + n_ngos} donors + NGOs do not fit on {rows * cols} junctions")

    nodes, roads = build_roads(rng, rows, cols, spacing)
    sites = rng.sample(list(nodes), n_donors + n_ngos)  # one site per junction

    types = [list(DONOR_TYPES)[i % len(DONOR_TYPES)] for i in range(n_donors)]
    rng.shuffle(types)
    donor_names = unique_names(rng, DONOR_PREFIXES, [DONOR_TYPES[t][0] for t in types], n_donors)
    donors, vehicles, packages = [], [], []
    for i, t in enumerate(types, 1):
        _, vtype, cap, speed, cost, lots, kg, expiry = DONOR_TYPES[t]
        donors.append((f"D{i}", donor_names[i - 1], t, sites[i - 1]))
        vehicles.append((f"V{i}", f"D{i}", vtype, max(5, round(cap * capacity_scale)), speed, cost))
        for item, cat in rng.sample(ITEMS[t], min(rng.randint(*lots), len(ITEMS[t]))):
            w = rng.randint(*kg)
            e = max(0.25, round(rng.uniform(*expiry) * expiry_scale * 4) / 4)  # quarter hours
            packages.append((f"P{len(packages) + 1}", f"D{i}", item, cat, round(w * rng.uniform(2.2, 2.8)), w, e))

    supply = {c: sum(p[5] for p in packages if p[3] == c) for c in CATEGORIES}
    profile_mix = PROFILES if diet == "mixed" else STRICT_PROFILES
    profiles = [rng.choices([p for p, _ in profile_mix], [w for _, w in profile_mix])[0] for _ in range(n_ngos)]
    for c in CATEGORIES:  # every category needs at least one NGO that accepts it
        if not any(c in p for p in profiles):
            profiles[rng.randrange(n_ngos)] = ["VEG", "NON_VEG", "VEGAN"]
    size = [rng.uniform(0.5, 1.5) for _ in range(n_ngos)]
    demand = [{c: 0 for c in CATEGORIES} for _ in range(n_ngos)]
    for c in CATEGORIES:
        takers = [j for j in range(n_ngos) if c in profiles[j]]
        total = supply[c] * demand_ratio * rng.uniform(0.8, 1.2)  # per-category shortage or surplus
        weight = sum(size[j] for j in takers)
        for j in takers:
            demand[j][c] = max(1, round(total * size[j] / weight))

    ngo_types = [rng.choice(list(NGO_TYPES)) for _ in range(n_ngos)]
    ngo_names = unique_names(rng, NGO_PREFIXES, [NGO_TYPES[t] for t in ngo_types], n_ngos)
    ngos = [(f"N{j + 1}", ngo_names[j], ngo_types[j], sites[n_donors + j],
             demand[j]["VEG"], demand[j]["NON_VEG"], demand[j]["VEGAN"]) for j in range(n_ngos)]

    out = Path(out)
    out.mkdir(parents=True, exist_ok=True)
    tables = {
        "nodes": (["node_id", "x_km", "y_km", "label"],
                  [(k, x, y, f"Junction {k[1:]}") for k, (x, y) in nodes.items()]),
        "roads": (["u", "v", "distance_km"], roads),
        "donors": (["donor_id", "name", "type", "node_id"], donors),
        "vehicles": (["vehicle_id", "donor_id", "vehicle_type", "capacity_kg", "speed_kmph", "cost_per_km_inr"], vehicles),
        "packages": (["package_id", "donor_id", "item", "category", "meals", "weight_kg", "expiry_hours"], packages),
        "ngos": (["ngo_id", "name", "type", "node_id", "demand_veg_kg", "demand_non_veg_kg", "demand_vegan_kg"], ngos),
    }
    for table, (header, rows_) in tables.items():
        with open(out / f"{table}.csv", "w", newline="", encoding="utf-8") as f:
            writer = csv.writer(f)
            writer.writerow(header)
            writer.writerows(rows_)

    if quiet:
        return
    demand_total = {c: sum(d[c] for d in demand) for c in CATEGORIES}
    print(f"{out}: {len(nodes)} junctions, {len(roads)} roads, {n_donors} donors, "
          f"{len(packages)} lots, {n_ngos} NGOs")
    for c in CATEGORIES:
        print(f"  {c:8} supply {supply[c]:5} kg   demand {demand_total[c]:5} kg")


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--name", required=True, help="output folder under data/ (or a path with --out)")
    ap.add_argument("--out", help="write to this folder instead of data/<name>")
    ap.add_argument("--rows", type=int, default=6)
    ap.add_argument("--cols", type=int, default=5)
    ap.add_argument("--donors", type=int, default=12)
    ap.add_argument("--ngos", type=int, default=15)
    ap.add_argument("--seed", type=int, default=7)
    ap.add_argument("--spacing", type=float, default=2.5, help="grid spacing in km")
    ap.add_argument("--demand-ratio", type=float, default=1.0, help="total demand / total supply")
    ap.add_argument("--expiry-scale", type=float, default=1.0, help="multiply every shelf life (0.4 = tight expiry)")
    ap.add_argument("--capacity-scale", type=float, default=1.0, help="multiply every vehicle capacity")
    ap.add_argument("--diet", choices=["mixed", "strict"], default="mixed",
                    help="strict: more vegetarian-only and vegan-only NGOs")
    a = ap.parse_args()
    if a.name in ("demo", "toy") and not a.out:
        raise SystemExit("demo and toy are hand-built test datasets; choose another name")
    generate(a.out or ROOT / "data" / a.name, a.rows, a.cols, a.donors, a.ngos, a.seed, a.spacing, a.demand_ratio,
             a.expiry_scale, a.capacity_scale, a.diet)


if __name__ == "__main__":
    main()
