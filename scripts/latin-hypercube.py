import numpy as np
from scipy.stats import qmc

# Define ranges
robot_range = [2, 100]
msg_range = [1, 60]

# Number of samples
n_samples = 400

# LHS sampler
sampler = qmc.LatinHypercube(d=2)
sample = sampler.random(n=n_samples)

# Scale to the ranges
scaled_sample = qmc.scale(sample, [robot_range[0], msg_range[0]], [robot_range[1], msg_range[1]])

# Optionally round if needed
scaled_sample = np.round(scaled_sample).astype(int)

output_file = "/Users/alexanderlupatsiy/Documents/Uni/Masterarbeit/lora-workspace/rlora/simulations/dynamicParams.ini"

with open(output_file, "w") as f:
    time_values =""
    number_of_nodes = ""
    for row in scaled_sample:
        node_val, ttnm_val = row
        time_values = time_values+","+str(round(60/ttnm_val,4))+"s"
        number_of_nodes=number_of_nodes+ ","+str(node_val)
    f.write("[General]\n")
    f.write(f"**.timeToNextMission = ${{ttnm={time_values[1:]}}}\n")
    f.write(f"**.numberOfNodes = ${{numberNodes={number_of_nodes[1:]} ! ttnm}}\n\n")

print(f"Config written to {output_file}")
