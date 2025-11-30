import os
import json
import re
import statistics
import msgpack
import msgpack_numpy as m
m.patch()  # allow numpy + float encoding

# Root directory
root_dir = os.path.join(os.getenv("rlora_root"), "data")

SAVE_AS_MSGPACK = False  # set False if you want normal JSON output again

# ----------------------------------------------------------
# Generic file handler
# ----------------------------------------------------------
def process_json_file(root, filename, modify_func):
    filepath = os.path.join(root, filename)
    try:
        with open(filepath, "r") as f:
            data = json.load(f)

        top_key = next(iter(data))
        experiment = data[top_key]

        # Skip already-processed files (flattened data)
        if isinstance(experiment.get("vectors"), list):
            if experiment["vectors"] and isinstance(experiment["vectors"][0], (int, float)):
                print(f"⚠️ Skipping already-processed file: {filename}")
                return

        # Clean metadata (remove quotes, drop unused)
        itervars = experiment.get("itervars", {})
        cleaned_itervars = {
            k: v.strip('"') if isinstance(v, str) else v for k, v in itervars.items()
        }
        experiment["itervars"] = cleaned_itervars
        for key in ["attributes", "config"]:
            experiment.pop(key, None)

        # Run custom handler
        modify_func(experiment)

        # Determine output path and save format
        if SAVE_AS_MSGPACK:
            msgpack_path = filepath.replace(".json", ".msgpack")
            with open(msgpack_path, "wb") as f:
                msgpack.dump(data, f, default=m.encode)

            # Delete original JSON only if .msgpack exists and non-empty
            if os.path.exists(msgpack_path) and os.path.getsize(msgpack_path) > 0:
                os.remove(filepath)
                print(f"✅ Converted and deleted {filename}")
            else:
                print(f"⚠️ Skipped deletion (empty or missing): {filename}")
        else:
            with open(filepath, "w") as f:
                json.dump(data, f, indent=2)
            print(f"✅ Processed {filename}")

    except Exception as e:
        print(f"❌ Failed to process {filepath}: {e}")



# ----------------------------------------------------------
# Custom file type logic
# ----------------------------------------------------------


# "vectors": [0.6, 0.1, 1.0, ...]
def modify_time_on_air(exp):
    """Compute Jain's Fairness Index over the time-on-air values."""
    vectors = exp.get("vectors", [])
    if not vectors:
        exp["vectors"] = []
        return

    # x_i values = sum of time-on-air per node
    times = [sum(vec["value"]) for vec in vectors]

    # Compute Jain's Fairness Index
    s = sum(times)
    sq = sum(t*t for t in times)
    n = len(times)

    fairness = (s * s) / (n * sq) if sq > 0 else 1.0

    # Overwrite with fairness index
    exp["vectors"] = fairness


# "vectors": [
#   {
#     "sent": [12, 8, 15],
#     "rec": [10, 8, 14]
#   }
# ]
def modify_received_id(exp):
    """Count absolute/positive occurrences of each ID.
    Output as compact dict of arrays:
    {
        "sent": [12, 8, 15],
        "rec": [10, 8, 14]
    }
    """
    id_stats = {}

    for vec in exp.get("vectors", []):
        values = vec.get("value", [])
        for val in values:
            mid = abs(val)
            if mid not in id_stats:
                id_stats[mid] = {"sent": 0, "rec": 0}
            id_stats[mid]["sent"] += 1
            if val >= 0:
                id_stats[mid]["rec"] += 1

    exp["vectors"] = {
        "sent": [s["sent"] for s in id_stats.values()],
        "rec": [s["rec"] for s in id_stats.values()],
    }


# {
#   "vectors": {
#     "missionId": [5696, 5697, 5698],
#     "propagation_time_rts": [null, 0.245, 0.389],
#     "propagation_time_fragment": [0.334, 0.400, 0.512]
#     "receivers": [4, 6, 1]
#   }
# }
def modify_mission_id(exp):
    """Compute propagation times (RTS and fragment based) per mission ID
    and count how many nodes received the mission message."""
    vectors = exp.get("vectors", [])
    if not vectors:
        exp["vectors"] = []
        return

    # --- Collect events per node ---
    mission_sent = {}      # node -> {mission_id: time}
    mission_rts = {}       # node -> {mission_id: time}
    mission_received = {}  # node -> {mission_id: time}

    for v in vectors:
        name = v.get("name", "")
        times = v.get("time", [])
        values = v.get("value", [])
        module = v.get("module", "")

        node_match = re.search(r"loRaNodes\[(\d+)\]", module)
        if not node_match:
            continue
        node = int(node_match.group(1))

        for t, val in zip(times, values):
            if val == -1:
                continue
            mid = int(val)
            if "missionIdFragmentSent" in name:
                mission_sent.setdefault(node, {})[mid] = t
            elif "missionIdRtsSent" in name:
                mission_rts.setdefault(node, {})[mid] = t
            elif "receivedMissionId" in name:
                mission_received.setdefault(node, {})[mid] = t

    # --- Collect all mission IDs ---
    all_mission_ids = set()
    for d in (mission_sent, mission_rts, mission_received):
        for node_data in d.values():
            all_mission_ids.update(node_data.keys())

    if not all_mission_ids:
        exp["vectors"] = []
        return

    # --- Compute propagation times ---
    mission_ids = []
    prop_rts = []
    prop_frag = []
    num_receivers = []

    for mid in sorted(all_mission_ids):
        # earliest RTS and fragment sends
        rts_times = [t for d in mission_rts.values() if mid in d for t in [d[mid]]]
        frag_times = [t for d in mission_sent.values() if mid in d for t in [d[mid]]]

        if not rts_times and not frag_times:
            continue

        first_rts = min(rts_times) if rts_times else None
        first_fragment = min(frag_times) if frag_times else None

        # all reception times for this mission
        rec_times = [d[mid] for d in mission_received.values() if mid in d]
        if not rec_times:
            continue
        last_rec = max(rec_times)
        receiver_count = len(rec_times)

        delay_rts = (last_rec - first_rts) if first_rts is not None else None
        delay_frag = (last_rec - first_fragment) if first_fragment is not None else None

        mission_ids.append(mid)
        prop_rts.append(round(delay_rts, 3) if delay_rts is not None else None)
        prop_frag.append(round(delay_frag, 3) if delay_frag is not None else None)
        num_receivers.append(receiver_count)

    exp["vectors"] = {
        "missionId": mission_ids,
        "propagation_time_rts": prop_rts,
        "propagation_time_fragment": prop_frag,
        "receivers": num_receivers,
    }



def modify_time_of_last_trajectory(exp):
    """Replace with only cleaned arrays of trajectory times (remove sentinel 92233720.368548)."""

    vectors = exp.get("vectors", [])
    cleaned = []

    for v in vectors:
        if not isinstance(v, dict):
            continue
        name = v.get("name", "")
        if "timeOfLastTrajectory" not in name:
            continue

        values = v.get("value", [])
        # Remove *all* occurrences of the invalid sentinel
        filtered = [val for val in values if abs(val - 92233720.368548) > 1e-3]
        if filtered:
            cleaned.append(filtered)

    # Replace with a clean array of arrays
    exp["vectors"] = cleaned


# ----------------------------------------------------------
# Dispatcher map (regex → handler)
# ----------------------------------------------------------
pattern_map = {
    r"^timeOnAir-(MassMobility|GaussMarkovMobility)-\d+\.json$": modify_time_on_air,
    r"^idReceived-(MassMobility|GaussMarkovMobility)-\d+\.json$": modify_received_id,
    r"^missionId-(MassMobility|GaussMarkovMobility)-\d+\.json$": modify_mission_id,
    r"^timeOfLastTrajectory-(MassMobility|GaussMarkovMobility)-\d+\.json$": modify_time_of_last_trajectory,
}


# ----------------------------------------------------------
# Walk directory and dispatch
# ----------------------------------------------------------
def process_all_json(root_dir):
    for root, _, files in os.walk(root_dir):
        for filename in files:
            for pattern, func in pattern_map.items():
                if re.match(pattern, filename):
                    process_json_file(root, filename, func)
                    break  # only process once per file


# ----------------------------------------------------------
# Entry point
# ----------------------------------------------------------
if __name__ == "__main__":
    process_all_json(root_dir)
