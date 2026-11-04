import sst
import os
import sys

# Minimal SDL tracing a multi-threaded C program with the Ariel DynamoRIO frontend.
#
# Environment:
#   ARIEL_DYNAMORIO_TEST_APP      path to the dyntrace_threads binary (required)
#   ARIEL_DYNAMORIO_TEST_CORES    Ariel core count, >= 2 (default 2); the app runs
#                                 cores-1 worker threads so every thread gets a core
#   ARIEL_DYNAMORIO_TEST_PAYLOAD  set to 1 to enable writepayloadtrace (default 0)
#   ARIEL_DYNAMORIO_TEST_STATFILE write statistics to this CSV file instead of the console

sst.setProgramOption("timebase", "1ps")

app = os.getenv("ARIEL_DYNAMORIO_TEST_APP")
if app is None or not os.path.exists(app):
    sys.exit(os.EX_CONFIG)

corecount = int(os.getenv("ARIEL_DYNAMORIO_TEST_CORES", "2"))
if corecount < 2:
    print("ERROR: ARIEL_DYNAMORIO_TEST_CORES must be at least 2 (main thread + one worker)")
    sys.exit(os.EX_CONFIG)

payload = os.getenv("ARIEL_DYNAMORIO_TEST_PAYLOAD", "0")

ariel = sst.Component("a0", "ariel.ariel")
ariel.addParams({
    "verbose": "0",
    "frontend": "ariel.frontend.dynamorio",
    "corecount": str(corecount),
    "maxcorequeue": "256",
    "maxissuepercycle": "2",
    "pipetimeout": "0",
    "executable": app,
    "arielmode": "1",
    "writepayloadtrace": payload,
    "appargcount": "1",
    "apparg0": str(corecount - 1),
})

ariel.setSubComponent("memmgr", "ariel.MemoryManagerSimple")

bus = sst.Component("bus", "memHierarchy.Bus")
bus.addParams({"bus_frequency": "2 Ghz"})

memctrl = sst.Component("memory", "memHierarchy.MemController")
memctrl.addParams({
    "clock": "1GHz",
    "addr_range_start": 0,
})

memory = memctrl.setSubComponent("backend", "memHierarchy.simpleMem")
memory.addParams({
    "access_time": "10ns",
    "mem_size": "512MiB",
})

for i in range(corecount):
    l1 = sst.Component("l1cache_%d" % i, "memHierarchy.Cache")
    l1.addParams({
        "cache_frequency": "2 Ghz",
        "cache_size": "64 KB",
        "coherence_protocol": "MSI",
        "replacement_policy": "lru",
        "associativity": "8",
        "access_latency_cycles": "1",
        "cache_line_size": "64",
        "L1": "1",
        "debug": "0",
    })

    cpu_link = sst.Link("cpu_cache_link_%d" % i)
    cpu_link.connect((ariel, "cache_link_%d" % i, "50ps"), (l1, "highlink", "50ps"))

    bus_link = sst.Link("l1_bus_link_%d" % i)
    bus_link.connect((l1, "lowlink", "50ps"), (bus, "highlink%d" % i, "50ps"))

mem_link = sst.Link("mem_bus_link")
mem_link.connect((bus, "lowlink0", "50ps"), (memctrl, "highlink", "50ps"))

sst.setStatisticLoadLevel(1)
statfile = os.getenv("ARIEL_DYNAMORIO_TEST_STATFILE")
if statfile:
    sst.setStatisticOutput("sst.statoutputcsv")
    sst.setStatisticOutputOptions({"filepath": statfile})
else:
    sst.setStatisticOutput("sst.statOutputConsole")
ariel.enableStatistics([
    "instruction_count", "read_requests", "write_requests", "no_ops",
    "fp_sp_ins", "fp_dp_ins", "fp_sp_ops", "fp_dp_ops",
])
