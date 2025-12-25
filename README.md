# RLoRa

This repo is my working folder for a LoRa MAC protocol study in OMNeT++.
I keep the simulation model in `src/`, configs in `simulations/`, and scripts to export/aggregate data in `scripts/`.

Below is a guide for running the whole pipeline, especially the
OMNeT++ simulation campaign.

## Repo layout (quick map)

- `src/`: C++/NED model code for the LoRa network and MAC protocols.
- `simulations/`: OMNeT++ configs (`omnetpp.ini` + parameter files).
- `scripts/shell/`: run scripts for the campaign + export helpers.
- `scripts/python/`: Python aggregation scripts for the exported JSON data.
- `data/`: exported per-run JSON vectors (input for aggregation).
- `data_aggregated/`: aggregated metrics (what I plot).
- `out/`: build output (binary ends up in `out/.../src/rlora`).

## Prerequisites 

- OMNeT++ (for `opp_runall`, `opp_run`, `opp_scavetool`).
- INET framework (this repo expects it at `../inet4.4` by default).
- C++ build toolchain for OMNeT++.

The Makefile uses `INET_ROOT`, so go into INET's directory and run source setenv. Do the same for Omnet++.

Some scripts also use `rlora_root`:

```
export rlora_root=/path/to/rlora
```

## Build the simulation binary

From the repo root:

```
make cleanall
make makefiles
make
```

This produces the runnable binary under `out/<mode>/src/rlora`.  
The scripts here reference `out/clang-release/src/rlora`, so adjust if needed.

## OMNeT++ simulation campaign (main part)

The campaign is defined in:
- `simulations/omnetpp.ini`
- `simulations/staticParams.ini`
- `simulations/dynamicParams.ini`
- `simulations/statistics.ini`

Key points:
- Parameter sweeps for `numberNodes` and `ttnm` (time-to-next-mission).
- Area sizes: 300m, 1000m, 5000m, 10000m.
- MAC protocols: Aloha, Csma, MeshRouter, IRSMiTra, RSMiTra, RSMiTraNR, MiRS, RSMiTraNAV.
- Mobility configs: `MassMobility` and `GaussMarkovMobility`.
- Output vectors/scalars are written to `simulations/results/` by default.

### Run a small test (single run)

This is how I sanity-check:

```
opp_run -u Cmdenv -n ./simulations:./src:./../inet4.4/src \
  -l ./../inet4.4/src/INET \
  -f ./simulations/omnetpp.ini \
  -c MassMobility \
  -r 0
```

Adjust the INET path if it is not at `../inet4.4`.

### Run the full campaign

I use the `opp_runall` scripts in `scripts/shell/`:

```
./scripts/shell/start-1.sh   # MassMobility
./scripts/shell/start-2.sh   # GaussMarkovMobility
```

Notes:
- They use `-j80` (80 parallel jobs). Reduce if your machine is smaller.
- They run a big range of run numbers (`-r 243200..255999`) that cover the
  parameter sweep. This is heavy and takes a long time.
- If you want to regenerate the start scripts, use:

```
./scripts/shell/populate-start-script.sh
```

## Export vectors/scalars to JSON

After simulations finish, the results are in `simulations/results/` as `.vec`
and `.txt`. Export them to JSON with:

```
./scripts/shell/exportData.sh
```

This script expects `rlora_root` to be set and writes JSON files into `data/`
under protocol/dimension folders (e.g., `data/Aloha/300m/...`).

## Aggregate metrics (Python)

Run the Python aggregators from the repo root. Example for node reachability:

```
python scripts/python/data-evaluation/aggregate_metrics/aggregate_node_reachability.py
```

Other aggregators live in:

```
scripts/python/data-evaluation/aggregate_metrics/
```

Outputs land in `data_aggregated/`, grouped by metric.

## Typical workflow

1) Build (`make cleanall`, `make makefiles`, `make`).
2) Run campaign (`start-1.sh`, `start-2.sh`).
3) Export with `exportData.sh`.
4) Aggregate metrics with the Python scripts.
5) Plot or inspect the JSON in `data_aggregated/`.

If anything fails, the first things to check are:
- Is `INET_ROOT` pointing to the right INET version?
- Is `rlora_root` set?
- Are the INET paths in the start scripts correct for the machine?
