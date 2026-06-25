# Adapted from astra-sim/examples/run_scripts/ns3/Ring_allgather_16npus.sh

import sst
import os
import json
import pathlib

astra_dir    = pathlib.Path('/home/prlavin/sst/sst-astra-2/astra-sim') # TODO - set from ASTRA_ROOT or smth

_filename = os.path.basename(__file__)

def checkFile(path):
    if not path.exists():
        raise FileNotFoundError(f"No such file: {path}")

def parseTopoFile(path):
    print(f"{_filename}: parsing topo_file: {path}")
    f = open(path, "r")
    try:
        data = json.load(f)
        logical_dims = data.get("logical-dims", [])
    finally:
        f.close()

    topo = ",".join(logical_dims)
    topo = '[' + topo + ']'
    print(f"{_filename}: Result: {topo}")

    return topo


examples_dir  = astra_dir / 'examples'
workload_file = examples_dir / 'workload/microbenchmarks/all_gather/16npus_1MB/all_gather'
system_file   = examples_dir / 'system/native_collectives/Ring_4chunks.json'
memory_file   = examples_dir / 'remote_memory/analytical/no_memory_expansion.json'
topo_file     = examples_dir / 'network/ns3/sample_16nodes_1D.json'

for f in [astra_dir, examples_dir, system_file, memory_file, topo_file]:
    checkFile(f)


topo = parseTopoFile(topo_file)


# Create components
workload = sst.Component('workload', 'astra.AstraWorkload')

workload.addParams(
    {
        "workloadConfig":        workload_file,
        "systemConfig":          system_file,
        "memoryConfig":          memory_file,
        "logicalTopologyConfig": topo,
        "commGroupConfig":      "empty",
    }
)
