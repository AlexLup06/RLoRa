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

TXT_PATTERN = re.compile(
    r"mac(?P<protocol>[A-Za-z0-9]+)-maxX(?P<maxX>[0-9]+m)-ttnm(?P<ttnm>[0-9.]+)s-"
    r"numberNodes(?P<nodes>[0-9]+)-m(?P<mobility>[A-Za-z]+)-rep(?P<rep>[0-9]+)\.txt$"
)

SIM_DURATION_SECONDS = 600.0


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


def _read_first_value(lines: List[str], label: str) -> float:
    try:
        idx = lines.index(label)
    except ValueError as exc:
        raise ValueError(f"Label '{label}' missing") from exc
    if idx + 1 >= len(lines):
        raise ValueError(f"No value after label '{label}'")
    try:
        return float(lines[idx + 1])
    except ValueError as exc:
        raise ValueError(f"Value after '{label}' is not numeric") from exc


def parse_file(path: str) -> Tuple[Dict[str, str], float]:
    """Parse txt file and return metadata + normalized data throughput."""
    filename = os.path.basename(path)
    match = TXT_PATTERN.match(filename)
    if not match:
        raise ValueError("Filename does not match expected pattern")

    protocol = match.group("protocol")
    max_x = match.group("maxX")
    ttnm = float(match.group("ttnm"))
    nodes = int(match.group("nodes"))

    with open(path, "r") as handle:
        lines = [line.strip() for line in handle.readlines() if line.strip()]
    data_bytes = _read_first_value(lines, "Effective Bytes")

    if nodes <= 1:
        raise ValueError("Number of nodes must be greater than 1")

    normalized = data_bytes / (SIM_DURATION_SECONDS * (nodes - 1))
    metadata = {
        "macProtocol": protocol,
        "dimensions": max_x,
        "timeToNextMission": round_half_up(60.0 / ttnm) if ttnm else 0,
        "numberNodes": nodes,
    }
    return metadata, normalized


def aggregate_normalized_data_throughput() -> None:
    """Aggregate normalized data throughput grouped by metadata (excluding mobility)."""
    groups: Dict[Tuple[str, Tuple[Tuple[str, str], ...]], List[float]] = defaultdict(
        list
    )
    meta_lookup: Dict[Tuple[str, Tuple[Tuple[str, str], ...]], Dict[str, str]] = {}
    protocol_dim_entries: Dict[str, Dict[str, List[Dict[str, object]]]] = defaultdict(
        lambda: defaultdict(list)
    )

    for root, _, files in os.walk(DATA_DIR):
        for filename in files:
            if not TXT_PATTERN.match(filename):
                continue

            path = os.path.join(root, filename)
            try:
                meta, value = parse_file(path)
            except Exception as exc:
                print(f"Skipping {path}: {exc}")
                continue

            key = (meta["macProtocol"], tuple(sorted(meta.items())))
            groups[key].append(value)
            meta_lookup[key] = meta

    for key, values in groups.items():
        protocol, _ = key
        stats = compute_stats(values)
        metadata = dict(meta_lookup[key])
        metadata.pop("macProtocol", None)
        dim = metadata.get("dimensions", "unknown")

        entry = {"metadata": metadata, "data": stats}
        protocol_dim_entries[protocol][dim].append(entry)

    output_dir = os.path.join(OUTPUT_DIR, "normalized-data-throughput")
    os.makedirs(output_dir, exist_ok=True)

    for protocol, dim_map in protocol_dim_entries.items():
        protocol_lower = str(protocol).lower()
        for dim, dim_entries in dim_map.items():
            dim_safe = re.sub(r"[^A-Za-z0-9_.-]+", "-", dim)
            payload = {
                "metadata": {
                    "protocol": protocol_lower,
                    "dimensions": dim,
                    "count": len(dim_entries),
                },
                "data": [
                    {
                        **entry,
                        "metadata": {
                            k: v
                            for k, v in entry["metadata"].items()
                            if k != "dimensions"
                        },
                    }
                    for entry in dim_entries
                ],
            }
            outfile = os.path.join(
                output_dir,
                f"{protocol_lower}_{dim_safe}_normalized-data-throughput.json",
            )
            with open(outfile, "w") as handle:
                json.dump(payload, handle, indent=2)
            print(f"Wrote {outfile}")


if __name__ == "__main__":
    aggregate_normalized_data_throughput()
