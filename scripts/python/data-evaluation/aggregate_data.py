import sys
from typing import Callable, Dict, List, Tuple

from aggregate_metrics import (
    aggregate_collision_per_node,
    aggregate_mac_efficiency,
    aggregate_node_reachibility,
    aggregate_normalized_data_throughput,
    aggregate_reception_success_ratio,
    aggregate_time_on_air,
)

Aggregator = Tuple[str, Callable[[], None]]


def run_all(aggregators: List[Aggregator]) -> None:
    """Run each aggregation step in sequence."""
    for name, func in aggregators:
        print(f"== Aggregating {name} ==")
        func()


def select_aggregators(
    all_aggs: List[Aggregator], requested: List[str]
) -> List[Aggregator]:
    """Filter aggregators by requested names (case-insensitive)."""
    if not requested:
        return all_aggs

    lookup: Dict[str, Aggregator] = {
        name.lower(): (name, func) for name, func in all_aggs
    }
    selected: List[Aggregator] = []
    missing: List[str] = []

    for item in requested:
        key = item.lower()
        if key in lookup:
            selected.append(lookup[key])
        else:
            missing.append(item)

    if missing:
        available = ", ".join(sorted(lookup.keys()))
        print(f"Unknown aggregator(s): {', '.join(missing)}")
        print(f"Available aggregators: {available}")
        sys.exit(1)

    return selected


if __name__ == "__main__":
    AGGREGATORS: List[Aggregator] = [
        ("timeOnAir", aggregate_time_on_air.aggregate_time_on_air),
        (
            "collisionPerNode",
            aggregate_collision_per_node.aggregate_collision_per_node,
        ),
        ("macEfficiency", aggregate_mac_efficiency.aggregate_mac_efficiency),
        (
            "normalizedDataThroughput",
            aggregate_normalized_data_throughput.aggregate_normalized_data_throughput,
        ),
        (
            "receptionSuccessRatio",
            aggregate_reception_success_ratio.aggregate_reception_success_ratio,
        ),
        (
            "nodeReachibility",
            aggregate_node_reachibility.aggregate_node_reachibility,
        ),
    ]
    requested = sys.argv[1:]
    run_all(select_aggregators(AGGREGATORS, requested))
