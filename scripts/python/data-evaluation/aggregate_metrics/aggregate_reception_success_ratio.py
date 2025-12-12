import json
import math
import os
import re
import statistics
from collections import defaultdict
from decimal import ROUND_HALF_UP, Decimal
from typing import Dict, Iterable, List, Tuple

try:
    from .data_paths import iter_matching_files
except ImportError:  # pragma: no cover - fallback for direct execution
    from data_paths import iter_matching_files

# Root directories (fall back to CWD if env var is not set)
BASE_DIR = os.getenv("rlora_root") or os.getcwd()
DATA_DIR = os.path.join(BASE_DIR, "data")
OUTPUT_DIR = os.path.join(BASE_DIR, "data_aggregated")

ID_RECEIVED_PATTERN = re.compile(r"^idReceived-.*\.json$")


def round_half_up(value: float) -> int:
    """Round halves up (20.44->20, 20.45->21)."""
    return int(Decimal(value).quantize(Decimal("1"), rounding=ROUND_HALF_UP))


def compute_stats(values: Iterable[float]) -> Dict[str, float]:
    values = list(values)
    if not values:
        raise ValueError("No values to aggregate")
    mean_val = statistics.mean(values)
    stdev_val = statistics.stdev(values) if len(values) > 1 else 0.0
    margin = 1.96 * stdev_val / math.sqrt(len(values)) if values else 0.0
    return {
        "count": len(values),
        "mean": mean_val,
        "std": stdev_val,
        "ci95": [mean_val - margin, mean_val + margin],
    }


def parse_seconds(value: str) -> float:
    """Parse a duration string like '4.0s' into seconds."""
    text = str(value)
    if text.endswith("s"):
        text = text[:-1]
    return float(text)


def normalize_metadata(meta: Dict[str, str]) -> Dict[str, object]:
    """Clean and normalize metadata for grouping."""
    normalized: Dict[str, object] = {}
    for key, val in meta.items():
        if key in {"mobility", "maxY"}:
            continue
        if key == "maxX":
            normalized["dimensions"] = str(val)
        elif key == "ttnm":
            seconds = parse_seconds(val)
            normalized["timeToNextMission"] = seconds
        elif key == "numberNodes":
            try:
                normalized[key] = int(val)
            except ValueError:
                normalized[key] = val
        else:
            normalized[key] = val
    return normalized


def read_ratio(path: str) -> Tuple[Dict[str, object], float]:
    """Load reception success ratio and metadata from flattened idReceived json."""
    with open(path, "r") as handle:
        payload = json.load(handle)

    if not isinstance(payload, dict):
        raise ValueError("Unexpected JSON payload")

    meta_raw = payload.get("metadata", {})
    results = payload.get("results", {})
    if "mean" not in results:
        raise ValueError("Missing 'mean' in results")

    meta = normalize_metadata(meta_raw)
    value = float(results["mean"])
    return meta, value


def aggregate_reception_success_ratio() -> None:
    """Aggregate reception success ratio grouped by metadata (excluding mobility)."""
    def flush_dimension(
        protocol: str,
        dimension: str,
        groups: Dict[Tuple[str, Tuple[Tuple[str, object], ...]], List[float]],
        meta_lookup: Dict[
            Tuple[str, Tuple[Tuple[str, object], ...]], Dict[str, object]
        ],
        output_dir: str,
    ) -> None:
        if not groups:
            return

        protocol_lower = str(protocol).lower()
        dim_entries: List[Dict[str, object]] = []
        for key, values in groups.items():
            stats = compute_stats(values)
            metadata = dict(meta_lookup[key])
            metadata.pop("macProtocol", None)
            dim_entries.append({"metadata": metadata, "data": stats})

        if not dim_entries:
            return

        dim_safe = re.sub(r"[^A-Za-z0-9_.-]+", "-", str(dimension))
        payload = {
            "metadata": {
                "protocol": protocol_lower,
                "dimensions": dimension,
                "count": len(dim_entries),
            },
            "data": [
                {
                    **entry,
                    "metadata": {
                        k: v for k, v in entry["metadata"].items() if k != "dimensions"
                    },
                }
                for entry in dim_entries
            ],
        }
        outfile = os.path.join(
            output_dir, f"{protocol_lower}_{dim_safe}_reception-success-ratio.json"
        )
        with open(outfile, "w") as handle:
            json.dump(payload, handle, indent=2)
        print(f"Wrote {outfile}")

    output_dir = os.path.join(OUTPUT_DIR, "reception-success-ratio")
    os.makedirs(output_dir, exist_ok=True)

    groups: Dict[Tuple[str, Tuple[Tuple[str, object], ...]], List[float]] = defaultdict(
        list
    )
    meta_lookup: Dict[Tuple[str, Tuple[Tuple[str, object], ...]], Dict[str, object]] = {}

    for protocol, dimension, path in iter_matching_files(DATA_DIR, ID_RECEIVED_PATTERN):
        if path is None:
            flush_dimension(protocol, dimension, groups, meta_lookup, output_dir)
            groups.clear()
            meta_lookup.clear()
            continue

        try:
            meta, value = read_ratio(path)
        except Exception as exc:
            print(f"Skipping {path}: {exc}")
            continue

        protocol_name = str(meta.get("macProtocol", "unknown"))
        key_meta = dict(meta)
        key = (protocol_name, tuple(sorted(key_meta.items())))
        groups[key].append(value)
        meta_lookup[key] = key_meta


if __name__ == "__main__":
    aggregate_reception_success_ratio()
