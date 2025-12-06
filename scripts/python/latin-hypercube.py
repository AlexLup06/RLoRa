import numpy as np
from scipy.stats import qmc, truncnorm
import os


BASE_DIR = os.getenv("rlora_root") or os.getcwd()

# Ranges
robot_range = [10, 100]  # node count
# time-to-next-mission choices (seconds): 0.1-0.9 step 0.1, then 1..120
ttnm_choices = [round(0.1 * i, 1) for i in range(1, 10)] + list(range(1, 121))

# LHS setup
n_samples = 400
seed = 45
sampler = qmc.LatinHypercube(d=2, seed=seed)
lhs_samples = sampler.random(n=n_samples)

# ------------------------------------------------------
# 1. ROBOT COUNT — truncated normal distribution
# ------------------------------------------------------

# Choose mean and std for the robot count distribution
robot_mu = 25  # average number of nodes
robot_sigma = 25  # spread

# Compute truncation bounds
a_r = (robot_range[0] - robot_mu) / robot_sigma
b_r = (robot_range[1] - robot_mu) / robot_sigma

robot_trunc = truncnorm(a_r, b_r, loc=robot_mu, scale=robot_sigma)

# Convert CDF samples → robot counts
robot_samples = robot_trunc.ppf(lhs_samples[:, 0])

# Clip to valid integer range
robot_samples = np.clip(robot_samples, robot_range[0], robot_range[1])
robot_samples = np.round(robot_samples).astype(int)

# ------------------------------------------------------
# 2. TIME TO NEXT MISSION — pick from discrete choices
# ------------------------------------------------------

def pick_ttnm(sample: float) -> float:
    """Map a [0,1) sample to a discrete ttnm choice."""
    idx = min(int(sample * len(ttnm_choices)), len(ttnm_choices) - 1)
    return float(ttnm_choices[idx])


ttnm_values = [pick_ttnm(s) for s in lhs_samples[:, 1]]

# ------------------------------------------------------
# 3. Write output file
# ------------------------------------------------------

output_file = os.path.join(BASE_DIR, "simulations", "dynamicParams.ini")
with open(output_file, "w") as f:
    time_values = ",".join([f"{round(v, 4)}s" for v in ttnm_values])
    number_of_nodes = ",".join(map(str, robot_samples))
    f.write("[General]\n")
    f.write(f"**.timeToNextMission = ${{ttnm={time_values}}}\n")
    f.write(f"**.numberOfNodes = ${{numberNodes={number_of_nodes} ! ttnm}}\n\n")

print(f"Config written to {output_file}")
