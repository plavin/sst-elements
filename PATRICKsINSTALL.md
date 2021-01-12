This document was written by Patrick Lavin and goes over installation of SST-Elements on Georgia Tech's Hive cluster.

You should first read the [Detailed Build and Installation Instructions for SST 10.1.x](http://sst-simulator.org/SSTPages/SSTBuildAndInstall10dot1dot0SeriesDetailedBuildInstructions/). That will give you a general idea of what needs to be done.

NOTE: On Hive, home directory space is limited, so you should adjust paths in this guide to begin at `~/data`. This means your scratch directory for downloading packages is `~/data/scratch` and your install directory should be `~/data/local`.

1. Load modules

```
module unload intel automake gcc-compatibility openmpi
module load cmake
module load gcc/9.2.0 python/3.7.4
```

2. Install OpenMPI

    [Follow the instructions in the SST install guide](http://sst-simulator.org/SSTPages/SSTBuildAndInstall10dot1dot0SeriesDetailedBuildInstructions/#openmpi-403-strongly-recommended)

3. Install Spack

    Follow the instructions [here](https://spack.io/about/) to install Spack.

4. Install SST-Core
```
spack install sst-core
spack load sst-core
```

5. Install Intel's PIN

    [Follow SST's instructions to install PIN 3.x](http://sst-simulator.org/SSTPages/SSTBuildAndInstall10dot1dot0SeriesAdditionalExternalComponents/#intel-pin-tool-313-98189)

5. Install SST-Elements

    NOTE: You may have trouble with the autotools installed on Hive. You can use the script `download_and_install_autotools.sh` in this directory if you run into issues involving libtool etc.


- Download this repo into `~/data/scratch/src/`.

- Run `autoconf.sh`

- Configure with:

    `./configure --prefix=$SST_ELEMENTS_HOME --with-pin=$PIN_HOME`

- Finish things up with `make all install`




