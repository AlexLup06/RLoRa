import os
from typing import Iterable, List, Optional, Pattern, Sequence, Tuple

MAC_PROTOCOL_DIRS = [
    "Aloha",
    "Csma",
    "MeshRouter",
    "RSMiTra",
    "IRSMiTra",
    "RSMiTraNR",
    "RSMiTraNAV",
    "MiRS",
]

DIMENSION_DIRS = ["300m", "1000m", "5000m", "10000m"]


def _read_env_list(env_var: str) -> List[str]:
    """Split a comma-separated env var into a cleaned list, or empty if unset."""
    raw = os.getenv(env_var, "")
    parts = [p.strip() for p in raw.split(",") if p.strip()]
    return parts


def selected_protocols() -> List[str]:
    """Return protocol directories to scan, honoring RLORA_PROTOCOLS env."""
    env_protocols = _read_env_list("RLORA_PROTOCOLS")
    return env_protocols or list(MAC_PROTOCOL_DIRS)


def selected_dimensions() -> List[str]:
    """Return dimension directories to scan, honoring RLORA_DIMENSIONS env."""
    env_dims = _read_env_list("RLORA_DIMENSIONS")
    return env_dims or list(DIMENSION_DIRS)


def iter_matching_files(
    data_dir: str,
    pattern: Pattern[str],
    protocols: Sequence[str] | None = None,
    dimensions: Sequence[str] | None = None,
) -> Iterable[Tuple[str, str, Optional[str]]]:
    """Yield (protocol, dimension, path) within known protocol/dimension folders.

    Emits a `(protocol, dimension, None)` sentinel after finishing each dimension
    directory so callers can flush per-subdir work. Prints per-subdirectory counts and
    progress every 1k files scanned.
    """
    protocols = list(protocols or selected_protocols())
    dimensions = list(dimensions or selected_dimensions())
    total_scanned = 0
    for protocol in protocols:
        for dimension in dimensions:
            base_dir = os.path.join(data_dir, protocol, dimension)
            if not os.path.isdir(base_dir):
                continue
            scanned_here = 0
            for root, _, files in os.walk(base_dir):
                for filename in files:
                    scanned_here += 1
                    total_scanned += 1
                    if pattern.match(filename):
                        yield (protocol, dimension, os.path.join(root, filename))
            yield (protocol, dimension, None)


__all__ = [
    "DIMENSION_DIRS",
    "MAC_PROTOCOL_DIRS",
    "selected_dimensions",
    "selected_protocols",
    "iter_matching_files",
]
