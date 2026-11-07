"""FoodLoop – interactive decision-support dashboard.

Run from the project folder (after build.bat):
    streamlit run app/streamlit_app.py

The UI only edits inputs and displays results. Every algorithm (Dijkstra, Greedy, Min Cost
Max Flow, Knapsack) runs in the C++ engine, called through engine.run_engine.
"""
import math
import re
import sys
import tempfile
from pathlib import Path

import altair as alt
import matplotlib.pyplot as plt
import networkx as nx
import pandas as pd
import streamlit as st

from engine import (CATEGORIES, DATA_DIR, ROOT, TABLES, TEXT_COLUMNS, EngineError, list_datasets, load_dataset,
                    load_params, run_engine, to_csv_text)

sys.path.insert(0, str(ROOT / "tools"))
from generate_dataset import generate  # noqa: E402

st.set_page_config(page_title="FoodLoop", page_icon="🍲", layout="wide", menu_items={})
st.markdown("<style>#MainMenu, footer, [data-testid='stToolbar'] {visibility: hidden;}</style>",
            unsafe_allow_html=True)

DONOR_COLOR = "#2e7d32"
NGO_COLOR = "#ef6c00"
TICK_COLOR = "#fdd835"  # visible on light and dark themes
CATEGORY_COLORS = alt.Scale(domain=CATEGORIES, range=["#43a047", "#e53935", "#8e24aa"])


def inr(paise):
    return f"₹{paise / 100:,.2f}"


@st.cache_data(show_spinner="Running the C++ optimisation engine…")
def optimise(csv_items, param_items):
    """Cached on the exact CSV text and parameters, so unchanged inputs are not re-run."""
    return run_engine(dict(csv_items), dict(param_items))


def generate_city(n_donors, n_ngos, seed, demand_ratio, expiry_scale):
    """Builds a random city with tools/generate_dataset.py and returns its tables."""
    side = max(4, math.ceil(math.sqrt((n_donors + n_ngos) * 1.5)))  # enough junctions, roughly 1/3 left empty
    with tempfile.TemporaryDirectory() as tmp:
        generate(tmp, side, side, n_donors, n_ngos, int(seed), demand_ratio=demand_ratio,
                 expiry_scale=expiry_scale, quiet=True)
        return {t: pd.read_csv(Path(tmp) / f"{t}.csv", dtype={c: str for c in TEXT_COLUMNS}) for t in TABLES}


def next_id(ids, prefix):
    """Next free id such as P15, from the numbers already used after the prefix."""
    nums = [int(m.group(1)) for i in ids.dropna() if (m := re.fullmatch(rf"{prefix}(\d+)", str(i)))]
    return f"{prefix}{max(nums, default=0) + 1}"


def append_row(df, row):
    return pd.concat([df, pd.DataFrame([row])], ignore_index=True)


# ------------------------------------------------------------------ sidebar: where the input data comes from

st.sidebar.title("🍲 FoodLoop")
st.sidebar.caption("Food redistribution with Min Cost Max Flow")

PROTECTED = {"toy", "demo", "city", "large"}  # shipped datasets the app never overwrites
# Datasets offered in the dashboard (the others are kept for tests and experiments).
DATASET_LABELS = {
    "demo": "Demo city – 4 donors, 5 NGOs",
    "city": "Generated city – 12 donors, 15 NGOs",
    "city_fleet": "Shared fleet – 3 depots, 9 vehicles",
    "lucknow_base": "Lucknow, Robin Hood Army – real data",
    "lucknow_s5": "Lucknow – few vehicles (scenario S-5)",
}
datasets = [d for d in DATASET_LABELS if d in list_datasets()]
if "editor_version" not in st.session_state:
    st.session_state.editor_version = 0
    st.session_state.gen_count = 0

source = st.sidebar.radio(
    "Input data", ["Saved dataset", "Generate a new city"],
    help="Whatever you pick here can still be changed in the ✏️ Inputs tab.")

if source == "Saved dataset":
    dataset = st.sidebar.selectbox("Dataset", datasets, index=0, format_func=lambda d: DATASET_LABELS.get(d, d))
    source_key = ("saved", dataset)
    load_source = lambda: load_dataset(dataset)  # noqa: E731

elif source == "Generate a new city":
    with st.sidebar.form("generate"):
        g_donors = st.slider("Donors", 2, 60, 8)
        g_ngos = st.slider("NGOs", 2, 60, 10)
        g_ratio = st.slider("Demand ÷ supply", 0.5, 2.0, 1.0, 0.1, help="Above 1 means a food shortage")
        g_expiry = st.slider("Shelf-life scale", 0.3, 2.0, 1.0, 0.1, help="Below 1 means food expires sooner")
        g_seed = st.number_input("Random seed", 0, 9999, 1, help="Same seed and settings give the same city")
        if st.form_submit_button("Generate city", type="primary") or "generated" not in st.session_state:
            st.session_state.generated = generate_city(g_donors, g_ngos, g_seed, g_ratio, g_expiry)
            st.session_state.gen_count += 1
    dataset = f"generated city #{st.session_state.gen_count}"
    source_key = ("generated", st.session_state.gen_count)
    load_source = lambda: st.session_state.generated  # noqa: E731

reset = st.sidebar.button("Reset edits", help="Discard every edit and addition and reload the input data")
if reset or st.session_state.get("source_key") != source_key:
    st.session_state.work = {k: v.copy() for k, v in load_source().items()}
    st.session_state.source_key = source_key
    st.session_state.added = {"lots": set(), "ngos": set(), "donors": set()}  # entries added with the forms
    st.session_state.editor_version += 1

# Model parameters come from the dataset's own params.csv (e.g. the base paper's costs), else config/params.csv.
params = load_params(dataset if source == "Saved dataset" else None)

st.sidebar.markdown(
    "**Pipeline**  \n"
    "1. Dijkstra – road distances  \n"
    "2. Feasibility – category, expiry, time windows  \n"
    "3. Greedy – expiry ranking & tiers  \n"
    "4. Min Cost Max Flow – allocation  \n"
    "5. Knapsack trips, or multi-stop routes for a shared fleet  \n"
    "6. Greedy baseline – comparison")

# ------------------------------------------------------------------ inputs tab (runs first: its edits feed the engine)

base = st.session_state.work
tabs = st.tabs(["📊 Overview", "✏️ Inputs", "⏳ Urgency", "📦 Allocation", "🗺️ Network", "🚚 Trips"])
tab_overview, tab_inputs, tab_urgency, tab_alloc, tab_network, tab_trips = tabs

with tab_inputs:
    st.subheader("Inputs")
    if "flash" in st.session_state:
        st.success(st.session_state.pop("flash"))
    node_ids = base["nodes"]["node_id"].tolist()
    node_label = dict(zip(base["nodes"]["node_id"], base["nodes"]["label"]))
    place = lambda n: f"{n} – {node_label.get(n, '')}"  # noqa: E731
    key = st.session_state.editor_version
    fleet = "depots" in base  # shared vehicles at depots, time windows, multi-stop routing

    # ---- forms: the only way to change the inputs; each change is applied below, then the page redraws
    st.markdown("#### ➕ Add or remove data")
    add_lot = add_ngo = add_donor = remove = None
    f_lot, f_ngo, f_donor, f_remove = st.tabs(
        ["🍱 Food donation", "🏠 NGO request", "🏪 Donor" if fleet else "🏪 Donor + vehicle", "🗑️ Remove"])
    with f_lot, st.form("add_lot", clear_on_submit=True):
        c = st.columns(3)
        donor_opts = base["donors"]["donor_id"].dropna().tolist()
        donor_names = dict(zip(base["donors"]["donor_id"], base["donors"]["name"]))
        lot_donor = c[0].selectbox("Donor", donor_opts, format_func=lambda d: f"{d} – {donor_names.get(d, '')}")
        lot_item = c[1].text_input("Food item", placeholder="e.g. Veg pulao")
        lot_cat = c[2].selectbox("Category", CATEGORIES)
        c = st.columns(3)
        lot_kg = c[0].number_input("Quantity (kg)", 1, 1000, 10)
        lot_meals = c[1].number_input("Meals (approx.)", 0, 5000, 25)
        lot_exp = c[2].number_input("Expires in (hours)", 0.0, 500.0, 4.0, 0.25, help="0 = already expired")
        if fleet:
            c = st.columns(2)
            lot_ready = c[0].number_input("Food ready at (hours)", 0.0, 500.0, 0.0, 0.25)
            lot_close = c[1].number_input("Pickup window closes at (hours)", 0.0, 500.0, 3.0, 0.25)
        if st.form_submit_button("Add donation", type="primary"):
            add_lot = dict(donor_id=lot_donor, item=lot_item.strip() or "Food item", category=lot_cat,
                           meals=lot_meals, weight_kg=lot_kg, expiry_hours=lot_exp)
            if fleet:
                add_lot.update(ready_hours=lot_ready, pickup_close_hours=max(lot_close, lot_ready))
    with f_ngo, st.form("add_ngo", clear_on_submit=True):
        c = st.columns(3)
        ngo_nm = c[0].text_input("NGO name", placeholder="e.g. Sunrise Shelter")
        ngo_type = c[1].selectbox("Type", ["orphanage", "shelter", "food_bank", "community_kitchen", "old_age_home"])
        ngo_node = c[2].selectbox("Location", node_ids, format_func=place)
        c = st.columns(3)
        ngo_dem = {cat: c[i].number_input(f"Needs {cat} (kg)", 0, 1000, 10 if cat == "VEG" else 0)
                   for i, cat in enumerate(CATEGORIES)}
        if fleet:
            c = st.columns(2)
            ngo_open = c[0].number_input("Opens at (hours)", 0.0, 100.0, 0.0, 0.25)
            ngo_close = c[1].number_input("Closes at (hours, 0 = no limit)", 0.0, 100.0, 0.0, 0.25)
        if st.form_submit_button("Add NGO", type="primary"):
            add_ngo = dict(name=ngo_nm.strip() or "New NGO", type=ngo_type, node_id=ngo_node,
                           **{f"demand_{cat.lower()}_kg": v for cat, v in ngo_dem.items()})
            if fleet:
                add_ngo.update(open_hours=ngo_open, close_hours=ngo_close if ngo_close > ngo_open else -1)
    with f_donor, st.form("add_donor", clear_on_submit=True):
        c = st.columns(3)
        don_nm = c[0].text_input("Donor name", placeholder="e.g. Green Leaf Cafe")
        don_type = c[1].selectbox("Type", ["restaurant", "supermarket", "hostel", "event_organizer"])
        don_node = c[2].selectbox("Location", node_ids, format_func=place, key="donor_node")
        veh_type, veh_cap, veh_speed, veh_cost = "van", 25, 25.0, 6.0
        if not fleet:  # donor mode: every donor brings its own vehicle
            c = st.columns(4)
            veh_type = c[0].selectbox("Vehicle", ["scooter", "auto_rickshaw", "mini_truck", "van", "cycle_cart"])
            veh_cap = c[1].number_input("Capacity (kg)", 1, 1000, 25)
            veh_speed = c[2].number_input("Speed (km/h)", 1.0, 100.0, 25.0)
            veh_cost = c[3].number_input("Cost (₹ per km)", 0.0, 200.0, 6.0)
        else:
            st.caption("Shared fleet: vehicles start at the depots, so a new donor needs no vehicle of its own.")
        if st.form_submit_button("Add donor", type="primary"):
            add_donor = dict(name=don_nm.strip() or "New donor", type=don_type, node_id=don_node,
                             vehicle_type=veh_type, capacity_kg=veh_cap, speed_kmph=veh_speed, cost_per_km_inr=veh_cost)
        st.caption("A new donor has no food yet: add its donations in the 🍱 tab afterwards.")

    with f_remove, st.form("remove", clear_on_submit=True):
        entries = ([("lot", r.package_id, f"Food lot {r.package_id} – {r.item} ({r.weight_kg} kg)")
                    for r in base["packages"].itertuples()]
                   + [("ngo", r.ngo_id, f"NGO {r.ngo_id} – {r.name}") for r in base["ngos"].itertuples()]
                   + [("donor", r.donor_id, f"Donor {r.donor_id} – {r.name} (with its vehicle and food)")
                      for r in base["donors"].itertuples()])
        picked = st.selectbox("Entry to remove", entries, format_func=lambda e: e[2], index=None,
                              placeholder="Choose a food lot, NGO or donor")
        if st.form_submit_button("Remove") and picked:
            remove = picked

    # ---- current inputs (read-only; change them with the forms above)
    st.markdown("#### 📋 Current inputs")
    st.caption("Change these with the forms above. **Reset edits** in the sidebar restores the original data.")
    donors, vehicles, packages, ngos = base["donors"], base["vehicles"], base["packages"], base["ngos"]
    v_donors, v_lots, v_ngos, v_roads = st.tabs([
        f"Donors ({len(donors)}) & vehicles ({len(vehicles)})" if fleet else f"Donors & vehicles ({len(donors)})",
        f"Food lots ({len(packages)})", f"NGOs ({len(ngos)})", "Road network"])
    with v_donors:
        if fleet:
            d1, d2 = st.columns(2)
            d1.markdown("**Donors**")
            d1.dataframe(donors, hide_index=True, width="stretch")
            d2.markdown("**Shared vehicles and their depots**")
            d2.dataframe(vehicles.merge(base["depots"], on="depot_id", how="left"), hide_index=True, width="stretch")
        else:
            st.dataframe(donors.merge(vehicles, on="donor_id", how="left"), hide_index=True, width="stretch")
    with v_lots:
        st.dataframe(packages, hide_index=True, width="stretch",
                     column_config={"weight_kg": "kg", "expiry_hours": "expires in (h)"})
    with v_ngos:
        st.dataframe(ngos, hide_index=True, width="stretch",
                     column_config={f"demand_{c.lower()}_kg": f"needs {c} (kg)" for c in CATEGORIES})
    with v_roads:
        r1, r2 = st.columns(2)
        r1.dataframe(base["nodes"], width="stretch", hide_index=True)
        r2.dataframe(base["roads"], width="stretch", hide_index=True)

    current = dict(base)

    # ---- apply an addition or removal, then redraw
    if add_lot or add_ngo or add_donor or remove:
        work = dict(current)
        if add_lot:
            pid = next_id(packages["package_id"], "P")
            work["packages"] = append_row(packages, dict(package_id=pid, **add_lot))
            st.session_state.added["lots"].add(pid)
            st.session_state.flash = f"Added donation {pid}: {add_lot['weight_kg']} kg {add_lot['item']}."
        if add_ngo:
            nid = next_id(ngos["ngo_id"], "N")
            work["ngos"] = append_row(ngos, dict(ngo_id=nid, **add_ngo))
            st.session_state.added["ngos"].add(nid)
            st.session_state.flash = f"Added NGO {nid}: {add_ngo['name']}."
        if add_donor:
            did, vid = next_id(donors["donor_id"], "D"), next_id(vehicles["vehicle_id"], "V")
            d = add_donor
            st.session_state.added["donors"].add(did)
            work["donors"] = append_row(donors, dict(donor_id=did, name=d["name"], type=d["type"], node_id=d["node_id"]))
            if not fleet:
                work["vehicles"] = append_row(vehicles, dict(
                    vehicle_id=vid, donor_id=did, vehicle_type=d["vehicle_type"], capacity_kg=d["capacity_kg"],
                    speed_kmph=d["speed_kmph"], cost_per_km_inr=d["cost_per_km_inr"]))
            st.session_state.flash = f"Added donor {did} ({d['name']}) with vehicle {vid}. Now add its food."
        if remove:
            kind, rid, text = remove
            for ids in st.session_state.added.values():
                ids.discard(rid)
            if kind == "lot":
                work["packages"] = packages[packages["package_id"] != rid].reset_index(drop=True)
            elif kind == "ngo":
                work["ngos"] = ngos[ngos["ngo_id"] != rid].reset_index(drop=True)
            else:
                for t in ("donors", "vehicles", "packages"):
                    if "donor_id" in work[t]:
                        work[t] = work[t][work[t]["donor_id"] != rid].reset_index(drop=True)
            st.session_state.flash = f"Removed {text}."
        st.session_state.work = work
        st.session_state.editor_version += 1
        st.rerun()


tables = {"nodes": base["nodes"], "roads": base["roads"], "donors": donors, "vehicles": vehicles,
          "packages": packages, "ngos": ngos}
if fleet:
    tables["depots"] = base["depots"]

try:
    result = optimise(tuple((k, to_csv_text(v)) for k, v in tables.items()), tuple(params.items()))
except EngineError as e:
    with tab_overview:
        st.error(f"The engine rejected the input: **{e}**\n\nFix it in the **Inputs** tab or reset your edits.")
    st.stop()

# Lookups used by several tabs.
donor_name = dict(zip(donors["donor_id"], donors["name"]))
ngo_name = dict(zip(ngos["ngo_id"], ngos["name"]))
lot_item = dict(zip(packages["package_id"], packages["item"]))
m, g = result["metrics"]["mcmf"], result["metrics"]["greedy"]


def allocation_frame(rows):
    df = pd.DataFrame(rows, columns=["tier", "package_id", "donor_id", "ngo_id", "category", "kg", "km", "cost_paise"])
    df.insert(2, "item", df["package_id"].map(lot_item))
    df["ngo"] = df["ngo_id"].map(ngo_name)
    df["cost (₹)"] = df.pop("cost_paise") / 100
    return df


alloc = allocation_frame(result["allocations"])
greedy_alloc = allocation_frame(result["greedy_allocations"])

# ------------------------------------------------------------------ new entries vs history
# Entries added with the forms are shown first in every tab; everything else goes under a History expander.

added = st.session_state.added
lot_ids, ngo_ids, donor_ids_all = set(packages["package_id"]), set(ngos["ngo_id"]), set(donors["donor_id"])
new_donors = added["donors"] & donor_ids_all
new_lots = (added["lots"] & lot_ids) | set(packages[packages["donor_id"].isin(new_donors)]["package_id"])
new_ngos = added["ngos"] & ngo_ids
lot_donor = dict(zip(packages["package_id"], packages["donor_id"]))
focus_donors = new_donors | {lot_donor[p] for p in new_lots}
has_new = bool(new_lots or new_ngos or new_donors)
new_ids = new_lots | new_ngos | new_donors


def is_new(df, lot_col="package_id", ngo_col="ngo_id"):
    """Rows that involve a new food lot or a new NGO."""
    mask = pd.Series(False, index=df.index)
    if lot_col in df:
        mask |= df[lot_col].isin(new_lots)
    if ngo_col in df:
        mask |= df[ngo_col].isin(new_ngos)
    return mask


def history(what):
    """Full-data section: folded away when there are new entries to show, plain otherwise."""
    return st.expander(f"📜 History – {what}") if has_new else st.container()


def new_heading():
    st.markdown("#### 🆕 Your new entries")


def tag(i):
    return f"🆕 {i}" if i in new_ids else i


delivered_by_lot = {}
for t in result["delivery"]["trips"]:
    for c in t["crates"]:
        delivered_by_lot[c["package_id"]] = delivered_by_lot.get(c["package_id"], 0) + c["kg"]
tier_of = {r["package_id"]: r["tier"] for r in result["priority_list"]}

# ------------------------------------------------------------------ overview

with tab_overview:
    st.subheader(f"Results for “{DATASET_LABELS.get(dataset, dataset)}”")
    st.caption(f"{len(donors)} donors · {len(packages)} food lots · {len(ngos)} NGOs · "
               f"supply {m['supply_kg']} kg · demand {m['demand_kg']} kg")

    # ---- one-sentence answer first
    saving = g["allocation_cost_paise"] - m["allocation_cost_paise"]
    extra = m["allocated_kg"] - g["allocated_kg"]
    if extra > 0:
        verdict = f"**{extra} kg more food** reaches NGOs than with Greedy"
    elif saving > 0:
        verdict = (f"the same food as Greedy, **{inr(saving)} cheaper** "
                   f"({saving / max(g['allocation_cost_paise'], 1) * 100:.1f}% less transport cost)")
    else:
        verdict = "the same result as Greedy on this data"
    st.info(f"Min Cost Max Flow meets **{m['demand_fulfilled_pct']:.1f}%** of NGO demand "
            f"and delivers **{m['delivered_kg']} kg** before expiry: {verdict}.", icon="🍲")

    # ---- four headline numbers
    def versus_greedy(col, label, value, diff, text, lower_is_better=False, help=None):
        """Metric with its difference from Greedy; no arrow when the two methods are equal."""
        if abs(diff) < 1e-9:
            col.metric(label, value, "same as Greedy", delta_color="off", delta_arrow="off", help=help, border=True)
        else:
            col.metric(label, value, text, delta_color="inverse" if lower_is_better else "normal", help=help,
                       border=True)

    k = st.columns(4)
    d = m["allocated_kg"] - g["allocated_kg"]
    versus_greedy(k[0], "Food allocated", f"{m['allocated_kg']} kg", d, f"{d:+} kg vs Greedy",
                  help=f"DF = allocated ÷ demand = {m['demand_fulfilled_pct']:.1f}%")
    k[1].metric("Delivered on time", f"{m['expiry_feasible_pct']:.0f}%", f"{m['delivered_kg']} of {m['allocated_kg']} kg",
                delta_color="off", delta_arrow="off", help="EF = delivered before expiry ÷ allocated", border=True)
    d = m["unallocated_surplus_kg"] - g["unallocated_surplus_kg"]
    versus_greedy(k[2], "Food left over", f"{m['unallocated_surplus_kg']} kg", d, f"{d:+} kg vs Greedy",
                  lower_is_better=True, help="US = supply − allocated (no feasible NGO needed it)")
    d = (m["allocation_cost_paise"] - g["allocation_cost_paise"]) / 100
    versus_greedy(k[3], "Transport cost", inr(m["allocation_cost_paise"]), d, f"{d:+,.2f} ₹ vs Greedy",
                  lower_is_better=True, help="TC = Σ kg × road km × R")

    # ---- what happened to each new entry
    if has_new:
        new_heading()
        cards = []
        for r in packages[packages["package_id"].isin(new_lots)].itertuples():
            rows = alloc[alloc["package_id"] == r.package_id]
            to = ", ".join(f"{x.ngo_id} {x.ngo} ({x.kg} kg)" for x in rows.itertuples()) or "—"
            got, kg = int(rows["kg"].sum()), int(r.weight_kg)
            done = delivered_by_lot.get(r.package_id, 0)
            status = ("✅ fully delivered" if done == kg else f"🟡 {done} of {kg} kg delivered" if done
                      else "⛔ expired / no NGO in reach" if got == 0 else "🟠 allocated, not delivered")
            cards.append({"entry": f"🍱 {r.package_id} – {r.item}", "details": f"{kg} kg {r.category}, "
                          f"expires in {r.expiry_hours:g} h (tier {tier_of.get(r.package_id, '–')})",
                          "goes to": to, "status": status})
        for r in ngos[ngos["ngo_id"].isin(new_ngos)].itertuples():
            need = sum(int(getattr(r, f"demand_{c.lower()}_kg")) for c in CATEGORIES)
            rows = alloc[alloc["ngo_id"] == r.ngo_id]
            got = int(rows["kg"].sum())
            frm = ", ".join(f"{x.package_id} {x.item} ({x.kg} kg)" for x in rows.itertuples()) or "—"
            status = "✅ demand fully met" if got >= need else f"🟡 {got} of {need} kg met" if got else "⛔ nothing received"
            cards.append({"entry": f"🏠 {r.ngo_id} – {r.name}", "details": f"needs {need} kg",
                          "goes to": f"receives from {frm}", "status": status})
        for r in donors[donors["donor_id"].isin(new_donors)].itertuples():
            n_lots = int((packages["donor_id"] == r.donor_id).sum())
            cards.append({"entry": f"🏪 {r.donor_id} – {r.name}", "details": f"{r.type} at {r.node_id}",
                          "goes to": f"{n_lots} food lot(s) listed above" if n_lots else "—",
                          "status": "✅ has food" if n_lots else "ℹ️ add a donation for this donor"})
        st.dataframe(pd.DataFrame(cards), hide_index=True, width="stretch")

    # ---- two charts and the full comparison
    with history("all donors and NGOs"):
        left, right = st.columns(2, gap="large")
        chart_h = 240  # whole chart incl. axes and legend (Streamlit sizes the container to it)
        with left:
            st.markdown("**Where the food goes**")
            outcome = pd.DataFrame([
                {"method": method, "outcome": label, "kg": mm[key_]}
                for method, mm in (("MCMF", m), ("Greedy", g))
                for label, key_ in (("Delivered", "delivered_kg"), ("Undelivered", "undelivered_kg"),
                                    ("Left over", "unallocated_surplus_kg"))])
            chart = alt.Chart(outcome).mark_bar(cornerRadiusEnd=3).encode(
                x=alt.X("kg:Q", stack="zero", title="kg"),
                y=alt.Y("method:N", title=None, sort=["MCMF", "Greedy"]),
                color=alt.Color("outcome:N", title=None, legend=alt.Legend(orient="bottom"),
                                scale=alt.Scale(domain=["Delivered", "Undelivered", "Left over"],
                                                range=["#2e7d32", "#f9a825", "#c62828"])),
                tooltip=["method", "outcome", "kg"]).properties(height=chart_h)
            st.altair_chart(chart, width="stretch")
        with right:
            st.markdown("**Demand met per NGO** (MCMF)")
            rows = []
            for r in ngos.itertuples():
                need = sum(int(getattr(r, f"demand_{c.lower()}_kg")) for c in CATEGORIES)
                got = int(alloc[alloc["ngo_id"] == r.ngo_id]["kg"].sum())
                if need:
                    rows.append({"ngo": r.ngo_id, "name": r.name, "received": got, "demand": need,
                                 "met %": round(100 * got / need, 1)})
            dem = pd.DataFrame(rows)
            bars = alt.Chart(dem).mark_bar(cornerRadiusEnd=3, color="#2e7d32").encode(
                x=alt.X("met %:Q", title="% of demand met", scale=alt.Scale(domain=[0, 100])),
                y=alt.Y("ngo:N", title=None, sort=alt.EncodingSortField("met %", order="ascending")),
                tooltip=["ngo", "name", "received", "demand", "met %"])
            st.altair_chart(bars.properties(height=max(chart_h, 26 * len(dem) + 60)), width="stretch")

    with st.expander("Full MCMF vs Greedy comparison"):
        rows = [
            ("Total donor supply (kg)", "supply_kg"), ("Total NGO demand (kg)", "demand_kg"),
            ("Allocated (kg)", "allocated_kg"), ("DF – demand fulfilment (%)", "demand_fulfilled_pct"),
            ("US – unallocated surplus (kg)", "unallocated_surplus_kg"),
            ("Delivered before expiry (kg)", "delivered_kg"), ("EF – expiry-feasible delivery (%)", "expiry_feasible_pct"),
            ("TC – allocation cost Σx·D·R (₹)", "allocation_cost_inr"), ("Trip cost (₹)", "trip_cost_inr"),
            ("Trips", "trips"), ("Low-load trips", "low_load_trips"), ("Average trip load (%)", "avg_utilisation_pct"),
            ("ET – allocation time (ms)", "execution_ms"),
        ]
        if fleet:
            rows += [("Vehicles hired", "vehicles_used"), ("Vehicle km", "km"),
                     ("Unmet pickup + delivery (kg)", "unmet_units"),
                     ("Base-paper objective (routing + hiring + penalty)", "objective")]
        table = pd.DataFrame({"Metric": [r[0] for r in rows],
                              "MCMF": [m[r[1]] for r in rows], "Greedy": [g[r[1]] for r in rows]})
        st.dataframe(table, hide_index=True, width="stretch",
                     column_config={"MCMF": st.column_config.NumberColumn(format="%.4g"),
                                    "Greedy": st.column_config.NumberColumn(format="%.4g")})
        st.caption("Both methods use the same data and the same feasibility rules.")

# ------------------------------------------------------------------ urgency

with tab_urgency:
    st.subheader("Greedy expiry prioritisation")
    t1, t2 = result["params"]["tier_boundaries_hours"]
    st.caption(f"Tier 1: expiry < {t1:g} h · Tier 2: {t1:g}–{t2:g} h · Tier 3: ≥ {t2:g} h. "
               "Lots are ranked earliest expiry first; ties go to the larger lot.")
    pri = pd.DataFrame(result["priority_list"])
    pri["donor"] = pri["donor_id"].map(donor_name)
    feas = {b["package_id"]: ", ".join(r["ngo_id"] for r in b["routes"]) or "none"
            for b in result["feasibility"]["by_lot"]}
    pri["feasible NGOs"] = pri["package_id"].map(feas)
    pri_cols = ["rank", "package_id", "item", "donor", "category", "kg", "expiry_hours", "tier", "feasible NGOs"]

    def urgency_chart(df):
        chart = alt.Chart(df).mark_bar(cornerRadiusEnd=3).encode(
            y=alt.Y("package_id:N", sort=alt.EncodingSortField("rank"), title=None),
            x=alt.X("expiry_hours:Q", title="Remaining shelf life (h, square-root scale)", scale=alt.Scale(type="sqrt")),
            color=alt.Color("tier:O", scale=alt.Scale(domain=[1, 2, 3], range=["#c62828", "#f9a825", "#2e7d32"])),
            tooltip=pri_cols)
        st.altair_chart(chart.properties(height=28 * len(df) + 60), width="stretch")

    if has_new:
        new_heading()
        mine = pri[pri["package_id"].isin(new_lots)]
        if len(mine):
            st.caption(f"Rank out of {len(pri)} lots; rank 1 is served first.")
            urgency_chart(mine)
            st.dataframe(mine[pri_cols], hide_index=True, width="stretch")
        else:
            st.caption("No new food lots yet: new NGOs and donors appear in the other tabs.")

    with history(f"all {len(pri)} food lots"):
        urgency_chart(pri)
        st.dataframe(pri[pri_cols], hide_index=True, width="stretch")
        f = result["feasibility"]
        cols = st.columns(5)
        cols[0].metric("Feasible pairs", f["kept"])
        cols[1].metric("Removed: incompatible", f["removed_incompatible"])
        cols[2].metric("Removed: unreachable", f["removed_unreachable"])
        cols[3].metric("Removed: too late / outside window", f["removed_expiry"] + f.get("removed_window", 0))
        cols[4].metric("Lots already expired", f["expired_lots"])

# ------------------------------------------------------------------ allocation

with tab_alloc:
    st.subheader("Min Cost Max Flow allocation, tier by tier")
    un = pd.DataFrame(result["unallocated"], columns=["package_id", "kg"])
    un.insert(1, "item", un["package_id"].map(lot_item))
    unmet = pd.DataFrame(result["unmet_demand"], columns=["ngo_id", "category", "kg"])
    unmet.insert(1, "ngo", unmet["ngo_id"].map(ngo_name))

    if has_new:
        new_heading()
        left, right = st.columns(2)
        with left:
            mine = alloc[is_new(alloc)]
            st.markdown(f"**MCMF** – {int(mine['kg'].sum())} kg")
            if len(mine):
                st.dataframe(mine, hide_index=True, width="stretch")
            else:
                st.caption("Nothing allocated.")
        with right:
            mine_g = greedy_alloc[is_new(greedy_alloc)]
            st.markdown(f"**Greedy baseline** – {int(mine_g['kg'].sum())} kg")
            if len(mine_g):
                st.dataframe(mine_g, hide_index=True, width="stretch")
            else:
                st.caption("Nothing allocated.")
        for df, text in ((un[is_new(un)], "Left over from your new lots"), (unmet[is_new(unmet)], "Unmet demand of your new NGOs")):
            if len(df):
                st.markdown(f"**{text}**")
                st.dataframe(df, hide_index=True, width="stretch")

    with history("all allocations"):
        tiers = pd.DataFrame(result["tiers"])
        tiers["lots"] = tiers["lots"].apply(", ".join)
        tiers["cost (₹)"] = tiers.pop("cost_paise") / 100
        st.dataframe(tiers.rename(columns={"supply_kg": "supply (kg)", "flow_kg": "placed (kg)",
                                           "augmentations": "augmenting paths"}),
                     hide_index=True, width="stretch")

        left, right = st.columns(2)
        with left:
            st.markdown(f"**MCMF** – {m['allocated_kg']} kg, TC {inr(m['allocation_cost_paise'])}")
            st.dataframe(alloc, hide_index=True, width="stretch")
        with right:
            st.markdown(f"**Greedy baseline** – {g['allocated_kg']} kg, TC {inr(g['allocation_cost_paise'])}")
            st.dataframe(greedy_alloc, hide_index=True, width="stretch")

        left, right = st.columns(2)
        with left:
            st.markdown("**Unallocated surplus (MCMF)**")
            if len(un):
                st.dataframe(un, hide_index=True, width="stretch")
            else:
                st.success("None")
        with right:
            st.markdown("**Unmet demand (MCMF)**")
            if len(unmet):
                st.dataframe(unmet, hide_index=True, width="stretch")
            else:
                st.success("None")

# ------------------------------------------------------------------ network

with tab_network:
    st.subheader("Donor → NGO allocation network")
    c1, c2 = st.columns([1, 3])
    method = c1.radio("Allocation", ["MCMF", "Greedy"], horizontal=True)
    show_routes = c1.checkbox("Show road routes (Dijkstra)", value=False)
    show_junctions = c1.checkbox("Label junctions", value=False)
    labels = {i: f"{tag(i)} – {n}" for i, n in {**donor_name, **ngo_name}.items()}
    options = (["🆕 Your new entries"] if has_new else []) + ["Everyone"] + list(labels)
    focus = c1.selectbox("Focus on", options, format_func=lambda i: labels.get(i, i),
                         help="Show only the arrows into or out of the chosen donor(s) or NGO(s)")
    if focus == "🆕 Your new entries":
        focus_set = new_ngos | focus_donors
    elif focus == "Everyone":
        focus_set = set()
    else:
        focus_set = {focus}

    pos = {r.node_id: (r.x_km, r.y_km) for r in base["nodes"].itertuples()}
    roads = nx.Graph()
    roads.add_nodes_from(pos)
    roads.add_edges_from((r.u, r.v) for r in base["roads"].itertuples())

    source_alloc = alloc if method == "MCMF" else greedy_alloc
    if focus == "🆕 Your new entries":  # only the flows of new lots or into new NGOs
        source_alloc = source_alloc[is_new(source_alloc)]
    flows = source_alloc.groupby(["donor_id", "ngo_id"])["kg"].sum()
    if focus_set and focus != "🆕 Your new entries":
        flows = flows[[bool(focus_set & set(pair)) for pair in flows.index]]
    donor_node = dict(zip(donors["donor_id"], donors["node_id"]))
    ngo_node = dict(zip(ngos["ngo_id"], ngos["node_id"]))

    # Scale symbols with the number of donors + NGOs so large datasets stay readable.
    entities = len(donor_node) + len(ngo_node)
    node_size, font_size = (900, 9) if entities <= 15 else (450, 7) if entities <= 40 else (220, 5)
    kg_labels = len(flows) <= 40
    if focus_set:  # the view is zoomed to the focused entities, so full-size symbols fit
        node_size, font_size, entities = 900, 9, 0

    fig, ax = plt.subplots(figsize=(10, 6.5) if entities <= 40 else (13, 10))
    nx.draw_networkx_edges(roads, pos, ax=ax, edge_color="#cfcfcf", width=2)
    nx.draw_networkx_nodes(roads, pos, ax=ax, node_size=25, node_color="#9e9e9e")
    if show_junctions:
        nx.draw_networkx_labels(roads, {k: (x, y - 0.35) for k, (x, y) in pos.items()}, ax=ax,
                                font_size=7, font_color="#757575")

    if show_routes:
        paths = {(r["donor"], r["ngo"]): r["path"] for r in result["routes"]}
        for (d, n) in flows.index:
            path = paths.get((d, n), [])
            nx.draw_networkx_edges(roads, pos, edgelist=list(zip(path, path[1:])), ax=ax,
                                   edge_color="#64b5f6", width=4, alpha=0.6)

    alloc_graph = nx.DiGraph()
    label_pos = {}
    for d in donor_node:
        alloc_graph.add_node(d)
        label_pos[d] = pos[donor_node[d]]
    for n in ngo_node:
        alloc_graph.add_node(n)
        label_pos[n] = pos[ngo_node[n]]
    # Entities on the same junction would overlap (and a zero-length arrow breaks the labels): fan them out.
    by_spot = {}
    for i, xy in label_pos.items():
        by_spot.setdefault(xy, []).append(i)
    for (x, y), ids in by_spot.items():
        for j, i in enumerate(ids[1:], 1):
            angle = 2 * math.pi * j / len(ids)
            label_pos[i] = (x + 0.6 * math.cos(angle), y + 0.6 * math.sin(angle))
    for (d, n), kg in flows.items():
        alloc_graph.add_edge(d, n, kg=int(kg))

    max_kg = max(flows.max(), 1) if len(flows) else 1
    widths = [1 + 6 * alloc_graph[u][v]["kg"] / max_kg for u, v in alloc_graph.edges]
    nx.draw_networkx_edges(alloc_graph, label_pos, ax=ax, width=widths, edge_color="#1565c0", alpha=0.75,
                           arrows=True, arrowsize=16 if entities <= 40 else 10, connectionstyle="arc3,rad=0.15",
                           node_size=node_size)
    if kg_labels:
        nx.draw_networkx_edge_labels(alloc_graph, label_pos, ax=ax, font_size=8, connectionstyle="arc3,rad=0.15",
                                     edge_labels={(u, v): f"{a['kg']} kg" for u, v, a in alloc_graph.edges(data=True)})
    nx.draw_networkx_nodes(alloc_graph, label_pos, nodelist=list(donor_node), ax=ax, node_shape="s",
                           node_color=DONOR_COLOR, node_size=node_size)
    nx.draw_networkx_nodes(alloc_graph, label_pos, nodelist=list(ngo_node), ax=ax, node_color=NGO_COLOR,
                           node_size=node_size)
    highlight = [i for i in alloc_graph.nodes if i in new_ids]
    if highlight:  # gold ring around entries added with the forms
        nx.draw_networkx_nodes(alloc_graph, label_pos, nodelist=highlight, ax=ax, node_color="none",
                               edgecolors="#fbc02d", linewidths=3, node_size=node_size * 1.6)
    nx.draw_networkx_labels(alloc_graph, label_pos, ax=ax, font_color="white", font_weight="bold",
                            font_size=font_size)
    for dep in result.get("depots", []):  # fleet mode: depots / volunteer hubs
        x, y = pos[dep["node_id"]]
        ax.scatter([x], [y], marker="^", s=node_size * 0.9, color="#616161", zorder=4)
        ax.annotate(dep["depot_id"], (x, y), textcoords="offset points", xytext=(0, -14), ha="center",
                    fontsize=font_size, color="#424242")
    ax.set_axis_off()
    ax.margins(0.08)
    if focus_set:  # zoom to the focused entities and their partners
        involved = focus_set | {x for pair in flows.index for x in pair}
        xs = [label_pos[i][0] for i in involved if i in label_pos]
        ys = [label_pos[i][1] for i in involved if i in label_pos]
        ax.set_xlim(min(xs) - 1.5, max(xs) + 1.5)
        ax.set_ylim(min(ys) - 1.5, max(ys) + 1.5)
    c2.pyplot(fig, width="stretch")
    plt.close(fig)

    c1.markdown(("<span style='color:#616161'>▲</span> depot &nbsp; " if result.get("depots") else "")
                + f"<span style='color:{DONOR_COLOR}'>■</span> donor &nbsp; "
                f"<span style='color:{NGO_COLOR}'>●</span> NGO"
                + (" &nbsp; <span style='color:#fbc02d'>◯</span> new" if has_new else ""), unsafe_allow_html=True)
    c1.caption("Arrow width ∝ kg allocated. Straight arcs show who supplies whom; enable road routes to see "
               "the Dijkstra shortest paths used for distance and cost."
               + ("" if kg_labels else " kg labels are hidden at this size: use **Focus on** to read them."))
    with c1.expander("📜 Who is who") if has_new else c1.container():
        legend = pd.DataFrame({"id": [tag(i) for i in list(donor_name) + list(ngo_name)],
                               "name": list(donor_name.values()) + list(ngo_name.values())})
        st.dataframe(legend, hide_index=True, width="stretch")

# ------------------------------------------------------------------ trips

with tab_trips:
  if fleet:
    st.subheader("Vehicle routes (multi-stop routing on the MCMF plan, with feedback)")
    fb = result.get("feedback", {})
    st.caption(f"Shared vehicles from {len(result.get('depots', []))} depots · each route picks up and delivers at "
               f"several stops within time windows · {fb.get('rounds', 1)} allocation-routing rounds, "
               f"{fb.get('blocked_pairs', 0)} pairs re-allocated by feedback.")
    routes_ = result["delivery"]["trips"]
    rows = []
    for t in routes_:
        for k, sp in enumerate(t["stops"], 1):
            rows.append({"vehicle": t["vehicle_label"], "stop": k, "kind": sp["kind"], "place": sp["place"],
                         "lot": sp["package_id"], "kg": sp["kg"], "arrive (h)": sp["arrive_hours"],
                         "service start (h)": sp["service_start_hours"], "depart (h)": sp["depart_hours"],
                         "load after (kg)": sp["load_after_kg"], "capacity_kg": t["capacity_kg"]})
    stops_df = pd.DataFrame(rows)
    und = pd.DataFrame(result["delivery"]["undelivered"], columns=["crate", "package_id", "ngo_id", "kg", "reason"])

    def show_routes(df):
        bars = alt.Chart(df).mark_bar(height=14).encode(
            y=alt.Y("vehicle:N", title=None), x=alt.X("service start (h):Q", title="Hours from planning time"),
            x2="depart (h):Q", color=alt.Color("kind:N", scale=alt.Scale(domain=["pickup", "delivery"],
                                                                          range=[DONOR_COLOR, NGO_COLOR])),
            tooltip=["vehicle", "stop", "kind", "place", "lot", "kg", "arrive (h)", "service start (h)",
                     "depart (h)", "load after (kg)"])
        st.altair_chart(bars.properties(height=34 * df["vehicle"].nunique() + 60), width="stretch")
        st.dataframe(df.drop(columns=["capacity_kg"]), hide_index=True, width="stretch")

    if has_new and len(stops_df):
        new_heading()
        mine = stops_df[stops_df["lot"].isin(new_lots) | stops_df["place"].isin(new_ngos | new_donors)]
        if len(mine):
            show_routes(stops_df[stops_df["vehicle"].isin(mine["vehicle"])])
        else:
            st.caption("No route serves your new entries.")
    with history(f"all {len(routes_)} routes"):
        if len(stops_df):
            show_routes(stops_df)
            st.caption("Green = pickup service, orange = delivery service; the load column is tracked after every stop.")
        else:
            st.info("No routes: nothing could be delivered.")
        st.markdown("**Not routed**")
        if len(und):
            st.dataframe(und, hide_index=True, width="stretch")
        else:
            st.success("None – every allocated kilogram is on a route.")
  else:
      st.subheader("Vehicle trips (0/1 Knapsack loading, MCMF plan)")
      p = result["params"]
      st.caption(f"Crates of up to {p['crate_kg']} kg · tier weights {p['tier_weights']} · shift {p['shift_hours']:g} h · "
                 "one NGO per trip · Greedy picks the most urgent destination, Knapsack picks the load.")
      trips = pd.DataFrame(result["delivery"]["trips"])
      und = pd.DataFrame(result["delivery"]["undelivered"], columns=["crate", "package_id", "ngo_id", "kg", "reason"])
      trip_cols = ["vehicle", "trip", "ngo_id", "depart_hours", "served_by_hours", "return_hours", "load_kg",
                   "capacity_kg", "utilisation_pct", "low_load", "km_one_way", "cost (₹)", "crates_text"]

      def show_trips(df):
          gantt = alt.Chart(df).mark_bar(cornerRadius=3, height=18).encode(
              y=alt.Y("vehicle:N", title=None),
              x=alt.X("depart_hours:Q", title="Hours from planning time"),
              x2="return_hours:Q",
              color=alt.Color("ngo_id:N", title="NGO"),
              opacity=alt.condition("datum.low_load", alt.value(0.45), alt.value(1.0)),
              tooltip=["vehicle", "trip", "ngo_id", "depart_hours", "served_by_hours", "return_hours", "load_kg",
                       "capacity_kg", "utilisation_pct", "cost (₹)", "crates_text"])
          served = alt.Chart(df).mark_tick(color=TICK_COLOR, thickness=2, size=20).encode(
              y="vehicle:N", x="served_by_hours:Q", tooltip=["vehicle", "trip", "served_by_hours"])
          st.altair_chart((gantt + served).properties(height=60 * df["vehicle"].nunique() + 60), width="stretch")
          st.dataframe(df[trip_cols].rename(columns={"crates_text": "crates"}), hide_index=True, width="stretch",
                       column_config={"utilisation_pct": st.column_config.ProgressColumn(
                           "load %", min_value=0, max_value=100, format="%.0f%%")})

      if len(trips):
          trips["vehicle"] = trips["vehicle_label"] if "vehicle_label" in trips else trips["donor_id"] + " " + trips["vehicle_id"]
          trips["crates_text"] = trips["crates"].apply(lambda cs: ", ".join(f"{c['crate']} ({c['kg']} kg)" for c in cs))
          trips["cost (₹)"] = trips["cost_paise"] / 100
          trips["has_new"] = trips["crates"].apply(lambda cs: any(c["package_id"] in new_lots for c in cs)) \
              | trips["ngo_id"].isin(new_ngos)

      if has_new:
          new_heading()
          mine = trips[trips["has_new"]] if len(trips) else trips
          if len(mine):
              show_trips(mine)
          else:
              st.caption("No trip carries your new entries.")
          mine_und = und[is_new(und)]
          if len(mine_und):
              st.markdown("**Not delivered**")
              st.dataframe(mine_und, hide_index=True, width="stretch")

      with history(f"all {len(trips)} trips"):
          if len(trips):
              show_trips(trips)
              st.caption("Bar = trip from departure to return; yellow tick = food served at the NGO. "
                         "Faded bars are low-load trips.")
          else:
              st.info("No trips: nothing was allocated, or nothing could be delivered.")
          st.markdown("**Undelivered crates**")
          if len(und):
              st.dataframe(und, hide_index=True, width="stretch")
          else:
              st.success("None – every allocated crate arrives before expiry.")
