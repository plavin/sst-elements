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
#include <sstream>
#include "sst_config.h"
#include "astraConnector.h"

#include "astra-sim/system/Sys.hh"
#include "astraSimpleNetworkAdapter.h"



using namespace SST;
using namespace SST::Astra;
using namespace SST::Interfaces;

int AstraConnector::parseTopo(const std::string& topo)
{
    std::stringstream ss(topo);
    std::string item;

    while (std::getline(ss, item, ',')) {
        if (!item.empty()) {
            logicalDims_.push_back(std::stoi(item));
            numNPUs_ *= std::stoi(item);
        }
    }

    queuesPerDim_ = std::vector<int>(logicalDims_.size(), numQueuesPerDim_);
    return 0;
}

AstraConnector::AstraConnector(ComponentId_t id, Params& params) : Component(id) {

    out = new Output("", 1, 0, Output::STDOUT);

    registerAsPrimaryComponent();
    primaryComponentDoNotEndSim();

    workloadConfig_        = params.find<std::string>("workloadConfig");
    systemConfig_          = params.find<std::string>("systemConfig");
    commGroupConfig_       = params.find<std::string>("commGroupConfig", "empty");
    logicalTopologyConfig_ = params.find<std::string>("logicalTopologyConfig");
    loggingConfig_         = params.find<std::string>("loggingConfig", "empty");
    numQueuesPerDim_       = params.find<int>("numQueuesPerDim",1);
    commScale_             = params.find<double>("commScale", 1.0);
    injectionScale_        = params.find<double>("injectionScale", 1.0);
    rendezvousProtocol_    = params.find<bool>("rendezvousProtocol", false);

    AstraSim::LoggerFactory::init(loggingConfig_);

    parseTopo(logicalTopologyConfig_);

    for (int i = 0; i < numNPUs_; i++) {
        linkControl_[i] = loadUserSubComponent<SST::Interfaces::SimpleNetwork>("linkControl" + std::to_string(i), ComponentInfo::SHARE_NONE, 1);
        networks_.push_back(new AstraSimpleNetworkAdapter(i, *this));
        systems_.push_back(new AstraSim::Sys(
                i, workloadConfig_, commGroupConfig_,
                systemConfig_, nullptr, networks_.back(), logicalDims_,
                queuesPerDim_, injectionScale_, commScale_, rendezvousProtocol_));
        //TODO: free these objects in desctructor
        //TODO: change nullptr to remote memory

    }

	//Analytical::AnalyticalRemoteMemory* mem =
    //    new Analytical::AnalyticalRemoteMemory(memory_configuration);

}

AstraConnector::AstraConnector() : Component() {}

SimTime_t AstraConnector::getCurrentSimTimeNanoWrapper() {
    return getCurrentSimTimeNano();
}

AstraConnector::~AstraConnector()
{
    delete out;
}

