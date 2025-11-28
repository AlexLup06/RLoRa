import numpy as np
from scipy.stats import qmc, truncnorm
import os


BASE_DIR = os.getenv("rlora_root") or os.getcwd()


# Ranges
robot_range = [2, 100]  # node count
msg_rate_range = [1/60, 10]  # messages per second

# LHS setup
n_samples = 200
seed = 42
sampler = qmc.LatinHypercube(d=2, seed=seed)
lhs_samples = sampler.random(n=n_samples)

# 1. Scale robot dimension uniformly
robot_samples = np.round(
    qmc.scale(lhs_samples[:, [0]], robot_range[0], robot_range[1]).flatten()
).astype(int)

# 2. Scale message rate dimension using truncated normal distribution
mu, sigma = 0.2, 0.25
a, b = (msg_rate_range[0] - mu) / sigma, (msg_rate_range[1] - mu) / sigma
trunc_norm = truncnorm(a, b, loc=mu, scale=sigma)
msg_rate_samples = trunc_norm.ppf(lhs_samples[:, 1])  # from CDF space to actual values

# Convert messages per second to seconds per message (ttnm)
ttnm_values = 1 / msg_rate_samples
ttnm_values = np.clip(ttnm_values, 0.1, 60)  # cap for simulation safety

# Write output
output_file = os.path.join(BASE_DIR, "simulations", "dynamicParams.ini")
with open(output_file, "w") as f:
    time_values = ",".join([f"{round(v, 4)}s" for v in ttnm_values])
    number_of_nodes = ",".join(map(str, robot_samples))
    f.write("[General]\n")
    f.write(f"**.timeToNextMission = ${{ttnm={time_values}}}\n")
    f.write(f"**.numberOfNodes = ${{numberNodes={number_of_nodes} ! ttnm}}\n\n")

print(f"Config written to {output_file}")