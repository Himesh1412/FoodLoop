"""Bridge between the Streamlit UI and the C++ engine (workflow step 11).

The UI keeps the dataset as pandas DataFrames. To run the optimisation, the tables and
parameters are written to a temporary folder as CSV files, bin/foodloop.exe is run with
--json, and its JSON output is returned as a dict. All algorithms stay in C++.
"""
import json
import subprocess
import tempfile
from pathlib import Path

import pandas as pd

ROOT = Path(__file__).resolve().parents[1]
ENGINE = ROOT / "bin" / "foodloop.exe"
DATA_DIR = ROOT / "data"
PARAMS_FILE = ROOT / "config" / "params.csv"

TABLES = ["nodes", "roads", "donors", "vehicles", "packages", "ngos"]
CATEGORIES = ["VEG", "NON_VEG", "VEGAN"]
TEXT_COLUMNS = {"node_id", "u", "v", "donor_id", "ngo_id", "package_id", "vehicle_id"}


class EngineError(Exception):
    """The engine rejected the input (validation error) or could not be run."""


def list_datasets():
    return sorted(p.name for p in DATA_DIR.iterdir() if (p / "packages.csv").exists())


def load_dataset(name):
    """Reads one dataset folder into {table name: DataFrame}."""
    folder = DATA_DIR / name
    return {t: pd.read_csv(folder / f"{t}.csv", dtype={c: str for c in TEXT_COLUMNS}) for t in TABLES}


def load_params():
    """Reads config/params.csv into {key: value as text}."""
    df = pd.read_csv(PARAMS_FILE, dtype=str)
    return dict(zip(df["key"], df["value"]))


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
    if not ENGINE.exists():
        raise EngineError(f"{ENGINE} not found. Run build.bat in the project folder first.")

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
