# Astra

This element library connect ASTRA-sim to SST, allowing users to use network simulators such as `merlin` and `kingsley`.

## Instructions for enabling Astra

1. Download and install ASTRA-sim

**Note: Ensure you have [our changes to the CMakeLists.txt](https://github.com/plavin/astra-sim/tree/cmake-install)**

```
cd astra-sim
export ASTRA_INSTALL=$(pwd)/install
mkdir build
cd build
cmake -DCMAKE_INSTALL_PREFIX=$ASTRA_INSTALL -DSPDLOG_INSTALL=1 ..
make install
```

2. Configure SST-Elements

`./configure --with-astrasim=$ASTRA_INSTALL`
