"""Dispatcher to run all metric aggregation steps."""

from typing import Callable, List, Tuple

from aggregate_metrics import aggregate_collision_rate, aggregate_time_on_air


Aggregator = Tuple[str, Callable[[], None]]


def run_all(aggregators: List[Aggregator]) -> None:
    """Run each aggregation step in sequence."""
    for name, func in aggregators:
        print(f"== Aggregating {name} ==")
        func()


if __name__ == "__main__":
    AGGREGATORS: List[Aggregator] = [
        ("timeOnAir", aggregate_time_on_air.aggregate_time_on_air),
        ("collisionRate", aggregate_collision_rate.aggregate_collision_rate),
    ]
    run_all(AGGREGATORS)
