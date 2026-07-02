# Adapted from astra-sim/examples/run_scripts/ns3/Ring_allgather_16npus.sh
# WARNING - Assumes a 16 endpoint network. This assumption is baked into `workload_file`, `topo_file`,
#  the Merlin config, and possibly in other astra input files.

import sst

import os
import sys
import json
import pathlib

from sst.merlin.base import *
from sst.merlin.endpoint import *
from sst.merlin.topology import *
from sst.merlin.interface import *
from sst.merlin.router import *

astra_dir    = pathlib.Path('/home/prlavin/sst/sst-astra-2/astra-sim') # TODO - set from ASTRA_ROOT
examples_dir  = astra_dir / 'examples'
workload_file = examples_dir / 'workload/microbenchmarks/all_gather/16npus_1MB/all_gather'
system_file   = examples_dir / 'system/native_collectives/Ring_4chunks.json'
memory_file   = examples_dir / 'remote_memory/analytical/no_memory_expansion.json'
topo_file     = examples_dir / 'network/ns3/sample_16nodes_1D.json'

DIR  = os.path.dirname(__file__)
FILE = os.path.basename(__file__)

sys.path.insert(0, DIR) # So the PlatformDefinition file can be found

def checkFile(path):
    if not path.exists():
        raise FileNotFoundError(f"No such file: {path}")

for f in [astra_dir, examples_dir, system_file, memory_file, topo_file]:
    checkFile(f)

def parseTopoFile(path):
    print(f"{FILE}: parsing topo_file: {path}")
    f = open(path, "r")
    try:
        data = json.load(f)
        logical_dims = data.get("logical-dims", [])
    finally:
        f.close()

    topo = ",".join(logical_dims)

    numNPUs = 1
    for i in topo.split(','):
        numNPUs *= int(i)

    topo = '[' + topo + ']'
    print(f"{FILE}: Result: {topo}")

    return topo, numNPUs

class AstraJob(Job):
    def __init__(self, job_id, size, workload):
        Job.__init__(self, job_id, size)
        self._declareClassVariables(["_workload"])
        self._workload = workload
    def getName(self):
        return "AstraJob"
    def build(self, nID, extraKeys):
        return (self._workload, f"port{nID}")

if __name__ == "__main__":

    # Create AstraSim component
    topo, numNPUs = parseTopoFile(topo_file)

    workload = sst.Component('workload', 'astra.AstraWorkload')
    workload.addParams({
        "workloadConfig":        workload_file,
        "systemConfig":          system_file,
        "memoryConfig":          memory_file,
        "logicalTopologyConfig": topo,
        "commGroupConfig":      "empty",
    })

    ep = AstraJob(0, 16, workload)

    # Merlin settings
    PlatformDefinition.loadPlatformFile("platform_file_dragon_eth128")
    PlatformDefinition.setCurrentPlatform("platform_dragon_eth128")

    system = System()
    system.allocateNodes(ep, "linear")
    system.build()
