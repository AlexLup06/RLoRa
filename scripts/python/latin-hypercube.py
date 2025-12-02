import numpy as np
from scipy.stats import qmc, truncnorm
import os


BASE_DIR = os.getenv("rlora_root") or os.getcwd()

# Ranges
robot_range = [10, 100]        # node count 
msg_rate_range = [1/120, 10] # messages per second

# LHS setup
n_samples = 400
seed = 45
sampler = qmc.LatinHypercube(d=2, seed=seed)
lhs_samples = sampler.random(n=n_samples)

# ------------------------------------------------------
# 1. ROBOT COUNT — truncated normal distribution
# ------------------------------------------------------

# Choose mean and std for the robot count distribution
robot_mu = 25       # average number of nodes
robot_sigma = 25    # spread

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
# 2. MESSAGE RATE — truncated normal distribution
# ------------------------------------------------------

mu, sigma = 0.05, 0.1   # mean=1/20s, std deviation
a, b = (msg_rate_range[0] - mu) / sigma, (msg_rate_range[1] - mu) / sigma
trunc_norm = truncnorm(a, b, loc=mu, scale=sigma)

msg_rate_samples = trunc_norm.ppf(lhs_samples[:, 1])

# Convert messages/sec → seconds/message (ttnm)
ttnm_values = 1 / msg_rate_samples
ttnm_values = np.clip(ttnm_values, 0.1, 60)

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
