import json
import math
import os
import re
import statistics
from collections import defaultdict
from decimal import ROUND_HALF_UP, Decimal
from typing import Dict, Iterable, List, Tuple


# Root directories (fall back to CWD if env var is not set)
BASE_DIR = os.getenv("rlora_root") or os.getcwd()
DATA_DIR = os.path.join(BASE_DIR, "data")
OUTPUT_DIR = os.path.join(BASE_DIR, "data_aggregated")

# Only aggregate this metric for now
TIME_ON_AIR_PATTERN = re.compile(r"^timeOnAir-.*\.json$")


def normalize_metadata(itervars: Dict[str, str]) -> Dict[str, str]:
    """Prepare metadata: drop mobility, combine dimensions, keep ttnm as seconds."""
    meta: Dict[str, str] = {
        k: v
        for k, v in itervars.items()
        if k not in {"mobility", "maxX", "maxY", "ttnm"}
    }

    max_x = itervars.get("maxX")
    max_y = itervars.get("maxY")
    if max_x and max_y:
        meta["dimensions"] = str(max_x)
    elif max_x:
        meta["dimensions"] = str(max_x)
    elif max_y:
        meta["dimensions"] = str(max_y)

    ttnm_raw = itervars.get("ttnm")
    if ttnm_raw is not None:
        seconds = parse_seconds(ttnm_raw)
        meta["timeToNextMission"] = seconds

    if "numberNodes" in meta:
        try:
            meta["numberNodes"] = int(meta["numberNodes"])
        except ValueError:
            pass

    return meta


def parse_seconds(value: str) -> float:
    """Parse a duration string like '4.0s' into seconds."""
    text = str(value)
    if text.endswith("s"):
        text = text[:-1]
    return float(text)


def round_half_up(value: float) -> int:
    """Round halves up (20.44->20, 20.45->21)."""
    return int(Decimal(value).quantize(Decimal("1"), rounding=ROUND_HALF_UP))


def extract_scalar(value) -> float:
    """Coerce the stored vector payload into a single numeric value.

    The processed timeOnAir files should already hold a single float, but we
    defensively handle small list/dict cases by averaging any numeric entries.
    """
    if isinstance(value, (int, float)):
        return float(value)

    numbers: List[float] = []
    if isinstance(value, list):
        for item in value:
            if isinstance(item, (int, float)):
                numbers.append(float(item))
            elif isinstance(item, dict) and "value" in item:
                numbers.extend(
                    float(v) for v in item["value"] if isinstance(v, (int, float))
                )

    if not numbers:
        raise ValueError("timeOnAir value is not numeric")
    if len(numbers) == 1:
        return numbers[0]
    return statistics.mean(numbers)


def compute_stats(values: Iterable[float]) -> Dict[str, float]:
    """Return mean, stdev, and 95% CI for a collection of numbers."""
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


def build_filename(metadata: Dict[str, str]) -> str:
    """Construct a filename from metadata values (excluding mobility)."""
    ordered_values = [str(metadata[key]) for key in sorted(metadata)]
    safe_parts = [re.sub(r"[^A-Za-z0-9_.-]+", "-", v) for v in ordered_values]
    return f"timeOnAir-{'-'.join(safe_parts)}.json"


def read_time_on_air(path: str) -> Tuple[Dict[str, str], float]:
    """Load metadata and scalar value from a flattened timeOnAir export."""
    with open(path, "r") as handle:
        payload = json.load(handle)

    if not isinstance(payload, dict):
        raise ValueError("Unexpected JSON payload")

    meta_raw = payload.get("metadata", {})
    value_raw = payload.get("results")

    meta = normalize_metadata(meta_raw)
    value = extract_scalar(value_raw)
    return meta, value


def aggregate_time_on_air() -> None:
    """Aggregate timeOnAir across files grouped by metadata (excluding mobility)."""
    groups: Dict[
        Tuple[str, Tuple[Tuple[str, str], ...]], List[float]
    ] = defaultdict(list)
    meta_lookup: Dict[Tuple[str, Tuple[Tuple[str, str], ...]], Dict[str, str]] = {}
    protocol_entries: Dict[str, List[Dict[str, object]]] = defaultdict(list)
    protocol_dim_entries: Dict[str, Dict[str, List[Dict[str, object]]]] = defaultdict(
        lambda: defaultdict(list)
    )

    for root, _, files in os.walk(DATA_DIR):
        for filename in files:
            if not TIME_ON_AIR_PATTERN.match(filename):
                continue

            path = os.path.join(root, filename)
            try:
                meta, value = read_time_on_air(path)
            except Exception as exc:
                print(f"Skipping {path}: {exc}")
                continue

            protocol = meta.get("macProtocol", "unknown")
            key = (protocol, tuple(sorted(meta.items())))
            groups[key].append(value)
            meta_lookup[key] = meta

    for key, values in groups.items():
        protocol, _ = key
        stats = compute_stats(values)
        metadata = dict(meta_lookup[key])
        metadata.pop("macProtocol", None)
        dim = metadata.get("dimensions", "unknown")

        entry = {"metadata": metadata, "data": stats}
        protocol_entries[protocol].append(entry)
        protocol_dim_entries[protocol][dim].append(entry)

    output_dir = os.path.join(OUTPUT_DIR, "time-on-air")
    os.makedirs(output_dir, exist_ok=True)

    for protocol, entries in protocol_entries.items():
        protocol_lower = str(protocol).lower()
        dims = sorted(protocol_dim_entries[protocol].keys())

        # Per-dimension files
        for dim, dim_entries in protocol_dim_entries[protocol].items():
            dim_safe = re.sub(r"[^A-Za-z0-9_.-]+", "-", dim)
            payload_dim = {
                "metadata": {
                    "protocol": protocol_lower,
                    "dimensions": dim,
                    "count": len(dim_entries),
                },
                "data": [
                    {**entry, "metadata": {k: v for k, v in entry["metadata"].items() if k != "dimensions"}}  # type: ignore[index]
                    for entry in dim_entries
                ],
            }
            outfile_dim = os.path.join(
                output_dir, f"{protocol_lower}_{dim_safe}_time-on-air.json"
            )
            with open(outfile_dim, "w") as handle:
                json.dump(payload_dim, handle, indent=2)
            print(f"Wrote {outfile_dim}")


if __name__ == "__main__":
    aggregate_time_on_air()
