import os
import subprocess
from collections import Counter

# Set the directory to scan
directory = "/nobackup/lupatisy/LoRa-Mac/simulations/results"

file_types = Counter()
count = 0

# Loop through all files in the directory (non-recursive)
for entry in os.scandir(directory):
    if entry.is_file():
        if count % 1000 == 0:
            print(count)
        count += 1
        try:
            # Run `file` command on the file
            result = subprocess.run(
                ["file", "--brief", entry.path],
                capture_output=True,
                text=True,
                check=True
            )
            file_type = result.stdout.strip()
            file_types[file_type] += 1
        except subprocess.CalledProcessError:
            print(f"Could not determine type for: {entry.name}")

# Print file types sorted by count
for file_type, count in file_types.most_common():
    print(f"{count} -> {file_type}")
