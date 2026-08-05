# Adapted from astra-sim/examples/run_scripts/ns3/Ring_allgather_16npus.sh
# WARNING - Assumes a 16 endpoint network. This assumption is baked into `workload_file`, `topo_file`,
#  the Merlin config, and possibly in other astra input files.

import sst

import os
import sys
import json
import pathlib
import argparse

from sst.merlin.base import *
from sst.merlin.endpoint import *
from sst.merlin.topology import *
from sst.merlin.interface import *
from sst.merlin.router import *

try:
    as_dir = os.environ['ASTRA_SIM']
except KeyError:
    print('Error: Environment variables ASTRA_SIM not set')
    sys.exit(1)

astra_dir    = pathlib.Path(as_dir) # TODO - set from ASTRA_ROOT
examples_dir  = astra_dir / 'examples'
system_file   = examples_dir / 'system/native_collectives/Ring_4chunks.json'
memory_file   = examples_dir / 'remote_memory/analytical/no_memory_expansion.json'

DIR  = os.path.dirname(__file__)
FILE = os.path.basename(__file__)

sys.path.insert(0, DIR) # So the PlatformDefinition file can be found

def checkFile(path):
    if not path.exists():
        raise FileNotFoundError(f"No such file: {path}")

def parseCommGroupConfig(comm_group_file):
    setNPUs = set()
    with open(comm_group_file, "r", encoding="utf-8") as f:
        comm_group_dict = json.load(f)
    for val in comm_group_dict.values():
        setNPUs = setNPUs.union(set(val))

    numNPUs = max(setNPUs) + 1

    # Sanity check - make sure all values in [1..numNPUs-1] have been found
    assert(set([*range(numNPUs)]) == setNPUs)
    return comm_group_dict, numNPUs

class AstraJob(Job):
    def __init__(self, job_id, size, workload):
        Job.__init__(self, job_id, size)
        self._declareClassVariables(["_workload"])
        self._workload = workload
    def getName(self):
        return "AstraJob"
    def build(self, nID, extraKeys):
        nic = self._workload.setSubComponent("nic", "astra.AstraNIC", nID)
        nic.addParams(extraKeys)
        return (self._workload, f"port{nID}")

if __name__ == "__main__":

    parser = argparse.ArgumentParser()
    parser.add_argument('-w', '--workload', required=True)
    args = parser.parse_args()
    workload_file = pathlib.Path(args.workload)
    comm_group_file = workload_file.with_suffix('.json')

    for f in [astra_dir, examples_dir, system_file, memory_file, comm_group_file]:
        checkFile(f)

    _, numNPUs = parseCommGroupConfig(comm_group_file)

    # Create AstraSim component
    workload = sst.Component('workload', 'astra.AstraWorkload')
    workload.addParams({
        "numNPUs":               numNPUs, # Easier to compute in Python so we don't need to parse JSON in C++
        "workloadConfig":        workload_file,
        "systemConfig":          system_file,
        "memoryConfig":          memory_file,
        "commGroupConfig":       comm_group_file,
    })

    ep = AstraJob(0, numNPUs, workload)

    # Merlin settings
    PlatformDefinition.loadPlatformFile("platform_file_dragon_eth128")
    PlatformDefinition.setCurrentPlatform("platform_dragon_eth128")

    system = System()
    system.allocateNodes(ep, "linear")
    system.build()

    sst.setStatisticOutput("sst.statOutputCSV", { "filepath" : "stats.csv", "separator" : "," } )
    sst.setStatisticLoadLevel(2)
    #sst.enableAllStatisticsForComponentType("astra.AstraNIC")
    sst.enableAllStatisticsForAllComponents()
