"""Bridge between the Streamlit UI and the C++ engine (workflow step 11).

The UI keeps the dataset as pandas DataFrames. To run the optimisation, the tables and
parameters are written to a temporary folder as CSV files, bin/foodloop.exe is run with
--json, and its JSON output is returned as a dict. All algorithms stay in C++.
"""
import json
import os
import subprocess
import threading
import tempfile
from pathlib import Path

import pandas as pd

ROOT = Path(__file__).resolve().parents[1]
ENGINE = ROOT / "bin" / ("foodloop.exe" if os.name == "nt" else "foodloop")
_BUILD_LOCK = threading.Lock()
DATA_DIR = ROOT / "data"
PARAMS_FILE = ROOT / "config" / "params.csv"

TABLES = ["nodes", "roads", "donors", "vehicles", "packages", "ngos"]
CATEGORIES = ["VEG", "NON_VEG", "VEGAN"]
TEXT_COLUMNS = {"node_id", "u", "v", "donor_id", "ngo_id", "package_id", "vehicle_id", "depot_id"}


class EngineError(Exception):
    """The engine rejected the input (validation error) or could not be run."""


def list_datasets():
    return sorted(p.name for p in DATA_DIR.iterdir() if (p / "packages.csv").exists())


def load_dataset(name):
    """Reads one dataset folder into {table name: DataFrame}."""
    folder = DATA_DIR / name
    tables = {t: pd.read_csv(folder / f"{t}.csv", dtype={c: str for c in TEXT_COLUMNS}) for t in TABLES}
    if (folder / "depots.csv").exists():  # fleet mode: shared vehicles start at depots
        tables["depots"] = pd.read_csv(folder / "depots.csv", dtype={c: str for c in TEXT_COLUMNS})
    return tables


def load_params(dataset=None):
    """{key: value as text} from the dataset's own params.csv if it has one, else config/params.csv."""
    path = DATA_DIR / dataset / "params.csv" if dataset else None
    df = pd.read_csv(path if path is not None and path.exists() else PARAMS_FILE, dtype=str)
    return dict(zip(df["key"], df["value"]))


def ensure_engine():
    """Compiles the C++ engine with g++ the first time it is needed (e.g. on a fresh cloud server)."""
    if ENGINE.exists():
        return
    with _BUILD_LOCK:
        if ENGINE.exists():
            return
        ENGINE.parent.mkdir(exist_ok=True)
        sources = sorted(str(p) for p in (ROOT / "cpp" / "src").glob("*.cpp"))
        result = subprocess.run(["g++", "-std=c++14", "-O2", "-I", str(ROOT / "cpp" / "include"), *sources,
                                 "-o", str(ENGINE)], capture_output=True, text=True)
        if result.returncode != 0:
            raise EngineError("could not build the C++ engine: " + result.stderr[-2000:])


def to_csv_text(df):
    """CSV text for the engine. Whole-number float columns (pandas turns ints into floats
    as soon as a cell is empty) are written as integers, because the engine rejects 12.0."""
    df = df.copy()
    for col in df.columns:
        if pd.api.types.is_float_dtype(df[col]):
            values = df[col].dropna()
            if len(values) and (values == values.round()).all():
                df[col] = df[col].astype("Int64")
    return df.to_csv(index=False)


def run_engine(csv_texts, params, repeat=5):
    """Runs the engine on {table name: CSV text} and {param key: value}; returns its JSON as a dict.

    Raises EngineError with the engine's message (for example a validation error) on failure.
    """
    try:
        ensure_engine()
    except FileNotFoundError:
        raise EngineError("the C++ engine is missing and g++ is not installed; run build.bat first")

    with tempfile.TemporaryDirectory() as tmp:
        folder = Path(tmp)
        for name, text in csv_texts.items():
            (folder / f"{name}.csv").write_text(text, encoding="utf-8")
        params_path = folder / "params.csv"
        lines = ["key,value"] + [f"{k},{v}" for k, v in params.items()]
        params_path.write_text("\n".join(lines) + "\n", encoding="utf-8")

        result = subprocess.run(
            [str(ENGINE), str(folder), "--json", "--params", str(params_path), "--repeat", str(repeat)],
            capture_output=True, text=True, timeout=120)

    if result.returncode != 0:
        message = result.stderr.strip().removeprefix("error: ").replace(str(folder), "<dataset>")
        raise EngineError(message or "the engine failed without a message")
    return json.loads(result.stdout)
