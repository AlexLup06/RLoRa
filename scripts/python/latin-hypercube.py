import numpy as np
import os

BASE_DIR = os.getenv("rlora_root") or os.getcwd()

# --- timeToNextMessage (TTNM) ---
# Logarithmic spacing between 0.1 and 120 seconds
min_val = 1
max_val = 120
N = 21
alpha = 2

x = np.linspace(min_val ** (1 / alpha), max_val ** (1 / alpha), N)
ttnm_values = np.round(x**alpha).astype(int)

ttnm_values = np.unique(ttnm_values)
ttnm_values = [0.1, 0.2, 0.4, 0.8] + ttnm_values.tolist()

# --- numberOfNodes ---
# Range: 10 to 100, 20 values
num_values = 20
min_nodes = 10
max_nodes = 100

power = 2

normalized = np.linspace(0, 1, num_values)
numberOfNodes = (
    (min_nodes + (max_nodes - min_nodes) * normalized**power).round().astype(int)
)
numberOfNodes = sorted(set(numberOfNodes.tolist()))

print("numberOfNodes =", numberOfNodes)
print("timeToNextMessage =", ttnm_values)


output_file = os.path.join(BASE_DIR, "simulations", "dynamicParams.ini")
with open(output_file, "w") as f:
    time_values = ",".join([f"{round(v, 4)}s" for v in ttnm_values])
    number_of_nodes = ",".join(map(str, numberOfNodes))
    f.write("[General]\n")
    f.write(f"**.timeToNextMission = ${{ttnm={time_values}}}\n")
    f.write(f"**.numberOfNodes = ${{numberNodes={number_of_nodes}}}\n\n")

print(f"Config written to {output_file}")
