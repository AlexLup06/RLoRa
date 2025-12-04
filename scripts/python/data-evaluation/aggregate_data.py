from typing import Callable, List, Tuple

from aggregate_metrics import (
    aggregate_collision_per_node,
    aggregate_mac_efficiency,
    aggregate_node_reachability,
    aggregate_normalized_data_throughput,
    aggregate_time_on_air,
)

Aggregator = Tuple[str, Callable[[], None]]


def run_all(aggregators: List[Aggregator]) -> None:
    """Run each aggregation step in sequence."""
    for name, func in aggregators:
        print(f"== Aggregating {name} ==")
        func()


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
            "nodeReachability",
            aggregate_node_reachability.aggregate_node_reachability,
        ),
    ]
    run_all(AGGREGATORS)
