import os
import json
import re

# Root directory to start from
value = os.getenv("rlora_root")
root_dir = value+"/data"





def formatBytesReceived(root,filename):
    filepath = os.path.join(root, filename)

    try:
        with open(filepath, "r") as f:
            data = json.load(f)

        # Top-level key (e.g., "MassMobility-...")
        top_key = next(iter(data))
        experiment = data[top_key]

        newVector = []

        # just loop through each vector and add the up the posisitive values into one single value. this is all received bytes
        # also add up the absolute values. this is the number of bytes that could have been received
        for vector in experiment.get("vectors", []):
            positiveSum = 0
            absoluteSum = 0

            for value in vector["value"]:
                if value >= 0:
                    positiveSum = positiveSum + value
                
                absoluteSum = absoluteSum + abs(value)

            valuePair = {
                "positiveSum":positiveSum,
                "absoluteSum":absoluteSum
            }

            newVector.append(valuePair)

        experiment["vectors"] = newVector
                

        # Write back the updated JSON
        with open(filepath, "w") as f:
            json.dump(data, f, indent=2)


    except Exception as e:
        print(f"❌ Failed to process {filepath}: {e}")


# TODO!!!!!
def  formatReceivedAndSentMissionId(root,filename):
    filepath = os.path.join(root, filename)

    try:
        with open(filepath, "r") as f:
            data = json.load(f)

        # Top-level key (e.g., "MassMobility-...")
        top_key = next(iter(data))
        experiment = data[top_key]

        # for each id in sentMissionId create an array and count the value in the receivedMissioId vectors. For each occuren put the time inside the array
        # now count the values in each array. this is the number of times the mission has been received. 
        # also subtract the smallest time from the largest. this is the time it took for the mission to propagate

        #for vector in experiment.get("vectors", []):
            

        # Write back the updated JSON
        with open(filepath, "w") as f:
            json.dump(data, f, indent=2)


    except Exception as e:
        print(f"❌ Failed to process {filepath}: {e}")


# for each file combine all vector values. then for each id count the absolute values and the positive ones
def formatReceivedId(root,filename):
    filepath = os.path.join(root, filename)

    try:
        with open(filepath, "r") as f:
            data = json.load(f)

        # map for each unique id a value consisting of the amout the abslute value has appeared and the positive value has appeared
        # iterate through each vector 
        id_stats = {}

        for vector in experiment.get("vectors", []):
            for value in vector.get("value", []):
                id_ = abs(value)

                if id_ not in id_stats:
                    id_stats[id_] = {"absoluteCount": 0, "positiveCount": 0}

                id_stats[id_]["absoluteCount"] += 1
                if value >= 0:
                    id_stats[id_]["positiveCount"] += 1

        # Overwrite "vectors" with one summary per ID
        experiment["vectors"] = [
            {
                "absoluteCount": stats["absoluteCount"],
                "positiveCount": stats["positiveCount"]
            }
            for id_, stats in id_stats.items()
        ]

    except Exception as e:
        print(f"❌ Failed to process {filepath}: {e}")

def formatEffectiveThroughput(root, filename):
    filepath = os.path.join(root, filename)
    try:
        with open(filepath, "r") as f:
            data = json.load(f)

        top_key = next(iter(data))
        experiment = data[top_key]
        vectors = experiment.get("vectors", [])

        if not vectors:
            return

        max_len = len(vectors[0]["value"])
        summed = [0] * max_len

        for vector in vectors:
            for i in range(max_len):
                summed[i] += vector["value"][i]

        experiment["vectors"] = [{"value": summed}]

        with open(filepath, "w") as f:
            json.dump(data, f, indent=2)

    except Exception as e:
        print(f"❌ Failed to process {filepath}: {e}")

def formatThroughput(root, filename):
    filepath = os.path.join(root, filename)
    try:
        with open(filepath, "r") as f:
            data = json.load(f)

        top_key = next(iter(data))
        experiment = data[top_key]
        vectors = experiment.get("vectors", [])

        if not vectors:
            return

        max_len = len(vectors[0]["value"])
        summed = [0] * max_len

        for vector in vectors:
            for i in range(max_len):
                summed[i] += vector["value"][i]

        experiment["vectors"] = [{"value": summed}]

        with open(filepath, "w") as f:
            json.dump(data, f, indent=2)

    except Exception as e:
        print(f"❌ Failed to process {filepath}: {e}")


def formatTimeOnAir(root,filename):
    filepath = os.path.join(root, filename)

    try:
        with open(filepath, "r") as f:
            data = json.load(f)

        # Top-level key (e.g., "MassMobility-...")
        top_key = next(iter(data))
        experiment = data[top_key]

        timesOnAir = []
        # iterate through each vector and add up the values. This gets us the time on Air for each node. then we can calculate Jains Fairness Index index
        for vector in experiment.get("vectors", []):
            sum = 0 
            for value in vector["value"]:
                sum += value
            timesOnAir.append(sum)

        experiment["vectors"]=timesOnAir

        # Write back the updated JSON
        with open(filepath, "w") as f:
            json.dump(data, f, indent=2)


    except Exception as e:
        print(f"❌ Failed to process {filepath}: {e}")
        
# Walk through all subdirectories
for root, dirs, files in os.walk(root_dir):
    for filename in files:
        if re.match(r'^timeOnAir-\d+\.json$', filename): 
            formatTimeOnAir(root,filename)

        #if re.match(r'^timeInQueue-\d+\.json$', filename): 
            # i think nothing to do. we just use the vectors to maybe plot all data or just calculate means

        if re.match(r'^throughput-\d+\.json$', filename): 
            formatThroughput(root,filename)

        if re.match(r'^effectiveThroughput-\d+\.json$', filename): 
            formatEffectiveThroughput(root,filename)

        if re.match(r'^idReceived-\d+\.json$', filename): 
            formatReceivedId(root,filename)

        #if re.match(r'^missionId-\d+\.json$', filename): 
         #   formatReceivedAndSentMissionId(root,filename)

        if re.match(r'^bytesReceived-\d+\.json$', filename): 
            formatBytesReceived(root,filename)
