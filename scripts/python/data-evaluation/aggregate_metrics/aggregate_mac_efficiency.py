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

TXT_PATTERN = re.compile(
    r"mac(?P<protocol>[A-Za-z0-9]+)-maxX(?P<maxX>[0-9]+m)-ttnm(?P<ttnm>[0-9.]+)s-"
    r"numberNodes(?P<nodes>[0-9]+)-m(?P<mobility>[A-Za-z]+)-(?P<run>[0-9]+)\.txt$"
)


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


def _read_values_after_label(lines: List[str], label: str) -> List[float]:
    """Return consecutive numeric values following a label line."""
    try:
        idx = lines.index(label)
    except ValueError as exc:
        raise ValueError(f"Label '{label}' missing") from exc

    values: List[float] = []
    for entry in lines[idx + 1 :]:
        try:
            values.append(float(entry))
        except ValueError:
            break
    if not values:
        raise ValueError(f"No numeric values after label '{label}'")
    return values


def parse_file(path: str) -> Tuple[Dict[str, str], float]:
    """Parse txt file and return metadata + mac efficiency."""
    filename = os.path.basename(path)
    match = TXT_PATTERN.match(filename)
    if not match:
        raise ValueError("Filename does not match expected pattern")

    protocol = match.group("protocol")
    max_x = match.group("maxX")
    ttnm = float(match.group("ttnm"))
    nodes = match.group("nodes")

    with open(path, "r") as handle:
        lines = [line.strip() for line in handle.readlines() if line.strip()]

    bytes_received = _read_values_after_label(lines, "Bytes Received")
    if len(bytes_received) < 2:
        raise ValueError("Expected two values after 'Bytes Received'")

    b_data = bytes_received[0]
    b_other = bytes_received[1]

    if b_other == 0:
        raise ValueError("Denominator (second 'Bytes Received' value) is zero")

    efficiency = b_data / b_other
    metadata = {
        "macProtocol": protocol,
        "dimensions": max_x,
        "timeToNextMission": ttnm,
        "numberNodes": int(nodes),
    }
    return metadata, efficiency


def aggregate_mac_efficiency() -> None:
    """Aggregate MAC efficiency from txt files grouped by metadata (excluding mobility)."""
    def flush_dimension(
        protocol: str,
        dimension: str,
        groups: Dict[Tuple[str, Tuple[Tuple[str, str], ...]], List[float]],
        meta_lookup: Dict[Tuple[str, Tuple[Tuple[str, str], ...]], Dict[str, str]],
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
            output_dir, f"{protocol_lower}_{dim_safe}_mac-efficiency.json"
        )
        with open(outfile, "w") as handle:
            json.dump(payload, handle, indent=2)
        print(f"Wrote {outfile}")

    output_dir = os.path.join(OUTPUT_DIR, "mac-efficiency")
    os.makedirs(output_dir, exist_ok=True)

    groups: Dict[Tuple[str, Tuple[Tuple[str, str], ...]], List[float]] = defaultdict(
        list
    )
    meta_lookup: Dict[Tuple[str, Tuple[Tuple[str, str], ...]], Dict[str, str]] = {}

    for protocol, dimension, path in iter_matching_files(DATA_DIR, TXT_PATTERN):
        if path is None:
            flush_dimension(protocol, dimension, groups, meta_lookup, output_dir)
            groups.clear()
            meta_lookup.clear()
            continue

        try:
            meta, efficiency = parse_file(path)
        except Exception as exc:
            print(f"Skipping {path}: {exc}")
            continue

        key = (meta["macProtocol"], tuple(sorted(meta.items())))
        groups[key].append(efficiency)
        meta_lookup[key] = meta


if __name__ == "__main__":
    aggregate_mac_efficiency()
