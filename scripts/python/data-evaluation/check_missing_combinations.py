import os
import re
from collections import Counter
from typing import Dict, Iterable, List, Tuple

try:
    from aggregate_metrics.data_paths import iter_matching_files, selected_dimensions, selected_protocols
except ImportError:  # pragma: no cover - fallback for direct execution
    from data_paths import iter_matching_files, selected_dimensions, selected_protocols  # type: ignore

# Root directories (fall back to CWD if env var is not set)
BASE_DIR = os.getenv("rlora_root") or os.getcwd()
DATA_DIR = os.path.join(BASE_DIR, "data")

# Parameter grid from simulations/dynamicParams.ini
TTNM_VALUES = [
    "0.1s",
    "0.2s",
    "0.4s",
    "0.8s",
    "1s",
    "2s",
    "4s",
    "6s",
    "9s",
    "12s",
    "16s",
    "20s",
    "25s",
    "30s",
    "36s",
    "42s",
    "49s",
    "56s",
    "63s",
    "72s",
    "80s",
    "90s",
    "99s",
    "109s",
    "120s",
]

NUMBER_NODE_VALUES = [
    10,
    11,
    12,
    14,
    16,
    19,
    22,
    26,
    30,
    35,
    40,
    46,
    52,
    59,
    66,
    74,
    82,
    91,
    100,
]

# Accept any filename containing ttnmXs-numberNodesY
FILENAME_PATTERN = re.compile(
    r"ttnm(?P<ttnm>[0-9.]+)s.*numberNodes(?P<nodes>[0-9]+)", re.IGNORECASE
)


def _all_target_pairs() -> List[Tuple[str, int]]:
    return [(ttnm, nn) for ttnm in TTNM_VALUES for nn in NUMBER_NODE_VALUES]


def _count_pairs(paths: Iterable[str]) -> Counter:
    counts: Counter = Counter()
    for path in paths:
        name = os.path.basename(path)
        match = FILENAME_PATTERN.search(name)
        if not match:
            continue
        ttnm_val = f"{match.group('ttnm')}s"
        nn_val = int(match.group("nodes"))
        counts[(ttnm_val, nn_val)] += 1
    return counts


def report_counts(counts: Counter) -> None:
    all_pairs = set(_all_target_pairs())
    present = set(counts.keys())
    missing = sorted(all_pairs - present)

    print("  Combinations present (count > 0):")
    for (ttnm, nn), cnt in sorted(counts.items()):
        print(f"    ttnm={ttnm}, numberNodes={nn}: {cnt}")

    if missing:
        print("  Missing combinations:")
        for ttnm, nn in missing:
            print(f"    ttnm={ttnm}, numberNodes={nn}")
    else:
        print("  No missing combinations.")


def scan() -> None:
    """Scan each protocol/dimension folder and report coverage of ttnm/nodes pairs."""
    protocols = selected_protocols()
    dimensions = selected_dimensions()

    current_paths: List[str] = []
    current_proto = None
    current_dim = None

    for protocol, dimension, path in iter_matching_files(
        DATA_DIR, re.compile(r".*"), protocols=protocols, dimensions=dimensions
    ):
        if path is None:
            # Finished this subdir
            if current_proto is not None and current_dim is not None:
                print(f"\n=== {current_proto}/{current_dim} ===")
                counts = _count_pairs(current_paths)
                report_counts(counts)
            current_paths = []
            current_proto = None
            current_dim = None
            continue

        current_proto = protocol
        current_dim = dimension
        current_paths.append(path)


if __name__ == "__main__":
    scan()
