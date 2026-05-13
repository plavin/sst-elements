// Copyright 2009-2026 NTESS. Under the terms
// of Contract DE-NA0003525 with NTESS, the U.S.
// Government retains certain rights in this software.
//
// Copyright (c) 2009-2026, NTESS
// All rights reserved.
//
// Portions are copyright of other developers:
// See the file CONTRIBUTORS.TXT in the top level directory
// of the distribution for more information.
//
// This file is part of the SST software package. For license
// information, see the LICENSE file in the top level directory of the
// distribution.
#include "sst_config.h"
#include "astra.h"

using namespace SST;
using namespace SST::astra;

astraNetworkBridge::astraNetworkBridge(ComponentId_t id, Params& params) : Component(id) {

    out = new Output("", 1, 0, Output::STDOUT);

    registerAsPrimaryComponent();
    primaryComponentDoNotEndSim();

    workloadConfig_        = params.find<std::string>("workloadConfig");
    systemConfig_          = params.find<std::string>("systemConfig");
    networkConfig_         = params.find<std::string>("networkConfig");
    memoryConfig_          = params.find<std::string>("memoryConfig");
    commGroupConfig_       = params.find<std::string>("commGroupConfig", "empty");
    logicalTopologyConfig_ = params.find<std::string>("logicalTopologyConfig");
    loggingConfig_         = params.find<std::string>("loggingConfig", "empty");
    numQueuesPerDim_       = params.find<int>("numQueuesPerDim",1);
    commScale_             = params.find<double>("commScale", 1.0);
    injectionScale_        = params.find<double>("injectionScale", 1.0);
    rendezvousProtocol_    = params.find<bool>("rendezvousProtocol", false);
/*
    int numNPUs_ = 1;
    std::vector<int> logicalDims_;
    std::vector<int> queuesPerDim_;
    */

    AstraSim::LoggerFactory::init(loggingConfig_);
    //TODO: parse topo config to get numNPUs_, logicalDims_, queuesPerDim_


}

astraNetworkBridge::astraNetworkBridge() : Component() {}

astraNetworkBridge::~astraNetworkBridge()
{
    delete out;
}

