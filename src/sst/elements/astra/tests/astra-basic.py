# Adapted from astra-sim/examples/run_scripts/ns3/Ring_allgather_16npus.sh
# WARNING - Assumes a 16 endpoint network. This assumption is baked into `workload_file`, `topo_file`,
#  the Merlin config, and possibly in other astra input files.

import sst
import os
import json
import pathlib
from sst.merlin import *

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

    numNPUs = 1
    for i in topo.split(','):
        numNPUs *= int(i)

    topo = '[' + topo + ']'
    print(f"{_filename}: Result: {topo}")

    return topo, numNPUs


examples_dir  = astra_dir / 'examples'
workload_file = examples_dir / 'workload/microbenchmarks/all_gather/16npus_1MB/all_gather'
system_file   = examples_dir / 'system/native_collectives/Ring_4chunks.json'
memory_file   = examples_dir / 'remote_memory/analytical/no_memory_expansion.json'
topo_file     = examples_dir / 'network/ns3/sample_16nodes_1D.json'

for f in [astra_dir, examples_dir, system_file, memory_file, topo_file]:
    checkFile(f)

topo, numNPUs = parseTopoFile(topo_file)

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


## Merlin Config
merlinTopo = topoTorus()

sst.merlin._params["torus.shape"] = "4x2x2"
sst.merlin._params["torus.width"] = "1x1x1"
sst.merlin._params["torus.local_ports"] = "1"
sst.merlin._params["num_dims"] = "3"

sst.merlin._params["link_bw"] = "100Gb/s"
sst.merlin._params["link_lat"] = "20ns"
sst.merlin._params["flit_size"] = "64b"
sst.merlin._params["xbar_bw"] = "100Gb/s" # Should this be higher?

sst.merlin._params["input_latency"] = "20ns"
sst.merlin._params["output_latency"] = "20ns"
sst.merlin._params["input_buf_size"] = "10kB"
sst.merlin._params["output_buf_size"] = "10kB"

sst.merlin._params["xbar_arb"] = "merlin.xbar_arb_lru"

class AstraEndPoint(EndPoint):
    def __init__(self, workload):
        EndPoint.__init__(self)
        self.workload = workload
    def getName(self):
        return "AstraEndPoint"
    def prepParams(self):
        pass
    def build(self, nID, extraKeys):
        return (self.workload, f"port{nID}", sst.merlin._params['link_lat'])

endPoint = AstraEndPoint(workload)

merlinTopo.prepParams()
# endPoint.prepParams()
merlinTopo.setEndPoint(endPoint)
merlinTopo.build()
