import os
import json
import re
import statistics
import msgpack
import msgpack_numpy as m
import math
from typing import Dict, List
from scipy import stats

m.patch() 

root_dir = os.path.join(os.getenv("rlora_root"), "data")

SAVE_AS_MSGPACK = False

def process_json_file(root, filename, modify_func):
    filepath = os.path.join(root, filename)
    try:
        with open(filepath, "r") as f:
            data = json.load(f)

        if isinstance(data, dict) and set(data.keys()) == {"metadata", "results"}:
            print(f"⚠️ Skipping already-processed file: {filename}")
            return

        top_key = next(iter(data))
        experiment = data[top_key]

        if not isinstance(experiment, dict):
            print(f"⚠️ Unexpected format, skipping: {filename}")
            return

        itervars = experiment.get("itervars", {})
        cleaned_itervars = {
            k: v.strip('"') if isinstance(v, str) else v for k, v in itervars.items()
        }
        experiment["itervars"] = cleaned_itervars
        for key in ["attributes", "config"]:
            experiment.pop(key, None)

        modify_func(experiment)

        new_data = {
            "metadata": experiment.get("itervars", {}),
            "results": experiment.get("vectors", {}),
        }

        if SAVE_AS_MSGPACK:
            msgpack_path = filepath.replace(".json", ".msgpack")
            with open(msgpack_path, "wb") as f:
                msgpack.dump(new_data, f, default=m.encode)

            if os.path.exists(msgpack_path) and os.path.getsize(msgpack_path) > 0:
                os.remove(filepath)
                print(f"✅ Converted and deleted {filename}")
            else:
                print(f"⚠️ Skipped deletion (empty or missing): {filename}")
        else:
            with open(filepath, "w") as f:
                json.dump(new_data, f, indent=2)
            print(f"✅ Processed {filename}")

    except Exception as e:
        print(f"❌ Failed to process {filepath}: {e}")

# "results": [0.678]
def modify_time_on_air(exp):
    """Compute Jain's Fairness Index over the time-on-air values."""
    vectors = exp.get("vectors", [])
    if not vectors:
        exp["vectors"] = []
        return

    times = [sum(vec["value"]) for vec in vectors]

    s = sum(times)
    sq = sum(t * t for t in times)
    n = len(times)

    fairness = (s * s) / (n * sq) if sq > 0 else 1.0

    exp["vectors"] = fairness


# "results": [
#   {
#       "mean": float,
#       "std": float,
#       "ci95": [lower_bound, upper_bound]
#   }
# ]
def modify_received_id(exp):
    """Compute reception success ratio from couldHavereceivedId vs receivedFragmentId."""
    vectors = exp.get("vectors", [])
    if not vectors:
        exp["vectors"] = []
        return

    possible: Dict[int, set] = {}
    received: Dict[int, set] = {}

    for vec in vectors:
        name = vec.get("name", "")
        name_lower = name.lower()
        values = vec.get("value", [])
        module = vec.get("module", "")

        node_match = re.search(r"[lL]o[Rr]aNodes\[(\d+)\]", module)
        if not node_match:
            continue
        node = int(node_match.group(1))

        for val in values:
            if val == -1:
                continue
            mid = abs(int(val))

            if "couldhavereceivedid:vector" in name_lower:
                possible.setdefault(mid, set()).add(node)
            elif "receivedfragmentid:vector" in name_lower:
                received.setdefault(mid, set()).add(node)

    ratios: List[float] = []
    for mid, nodes_possible in possible.items():
        denom = len(nodes_possible)
        if denom == 0:
            continue
        rec_nodes = received.get(mid, set())
        num = len(rec_nodes)
        ratios.append(num / denom)

    if not ratios:
        exp["vectors"] = []
        return

    n = len(ratios)
    mean = sum(ratios) / n
    std = math.sqrt(sum((x - mean) ** 2 for x in ratios) / (n - 1)) if n > 1 else 0
    ci95_margin = stats.t.ppf(0.975, df=n - 1) * (std / math.sqrt(n)) if n > 1 else 0

    exp["vectors"] = {
        "mean": mean,
        "std": std,
        "ci95": [mean - ci95_margin, mean + ci95_margin],
    }

# {
#   "results": {
#     "propagation_time": {"mean": 0.245, "std": 0.05, "ci95": [0.21, 0.28]},
#     "receiver_ratio": {"mean": 0.6, "std": 0.1, "ci95": [0.55, 0.65]},
#     "receiver_ratio_full": 0.4
#   }
# }
def modify_mission_id(exp):
    """Compute propagation time per mission ID and how broadly it was received."""
    vectors = exp.get("vectors", [])
    if not vectors:
        exp["vectors"] = []
        return

    mission_rts = {}
    mission_received = {}
    seen_nodes = set()

    for v in vectors:
        name = v.get("name", "")
        times = v.get("time", [])
        values = v.get("value", [])
        module = v.get("module", "")

        node_match = re.search(r"loRaNodes\[(\d+)\]", module)
        if not node_match:
            continue
        node = int(node_match.group(1))
        seen_nodes.add(node)

        for t, val in zip(times, values):
            if val == -1:
                continue
            mid = int(val)
            if "missionIdRtsSent" in name:
                mission_rts.setdefault(node, {})[mid] = t
            elif "receivedMissionId" in name:
                mission_received.setdefault(node, {})[mid] = t

    total_nodes = (max(seen_nodes) + 1) if seen_nodes else 0
    denom = total_nodes - 1

    all_mission_ids = set()
    for d in (mission_rts, mission_received):
        for node_data in d.values():
            all_mission_ids.update(node_data.keys())

    if not all_mission_ids:
        exp["vectors"] = []
        return

    prop_times = []
    receiver_ratio = []

    for mid in sorted(all_mission_ids):
        rts_times = [t for d in mission_rts.values() if mid in d for t in [d[mid]]]
        if not rts_times:
            continue

        first_rts = min(rts_times) if rts_times else None

        rec_times = [d[mid] for d in mission_received.values() if mid in d]
        if not rec_times:
            continue
        last_rec = max(rec_times)

        delay_rts = (last_rec - first_rts) if first_rts is not None else None
        ratio = (len(rec_times) / denom) if denom > 0 else None

        prop_times.append(round(delay_rts, 3) if delay_rts is not None else None)
        receiver_ratio.append(round(ratio, 3) if ratio is not None else None)

    def summarize(values):
        valid = [v for v in values if v is not None]
        if not valid:
            return {"mean": None, "std": None, "ci95": [None, None]}
        n = len(valid)
        mean = sum(valid) / n
        if n > 1:
            variance = sum((x - mean) ** 2 for x in valid) / (n - 1)
            std = math.sqrt(variance)
            margin = stats.t.ppf(0.975, df=n - 1) * (std / math.sqrt(n))
        else:
            std = 0.0
            margin = 0.0
        return {
            "mean": round(mean, 3),
            "std": round(std, 3),
            "ci95": [round(mean - margin, 3), round(mean + margin, 3)],
        }

    prop_times_full = [
        pt
        for pt, rr in zip(prop_times, receiver_ratio)
        if rr is not None and math.isclose(rr, 1.0, rel_tol=1e-9, abs_tol=1e-9)
    ]
    receiver_ratio_valid = [rr for rr in receiver_ratio if rr is not None]

    full_count = sum(
        1
        for rr in receiver_ratio_valid
        if math.isclose(rr, 1.0, rel_tol=1e-9, abs_tol=1e-9)
    )
    full_ratio = (
        round(full_count / len(receiver_ratio_valid), 3)
        if receiver_ratio_valid
        else None
    )

    exp["vectors"] = {
        "propagation_time": summarize(prop_times_full),
        "receiver_ratio": summarize(receiver_ratio_valid),
        "receiver_ratio_full": full_ratio,
    }

pattern_map = {
    r"^timeOnAir-.*\.json$": modify_time_on_air,
    r"^idReceived-.*\.json$": modify_received_id,
    r"^missionId-.*\.json$": modify_mission_id,
}

def process_all_json(root_dir):
    for root, _, files in os.walk(root_dir):
        for filename in files:
            for pattern, func in pattern_map.items():
                if re.match(pattern, filename):
                    process_json_file(root, filename, func)
                    break 

if __name__ == "__main__":
    process_all_json(root_dir)
