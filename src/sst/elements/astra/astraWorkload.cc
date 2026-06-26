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
#include "astraWorkload.h"

#include "astra-sim/system/Sys.hh"
#include "astraNetworkInterface.h"
#include "analytical/AnalyticalRemoteMemory.hh"


using namespace SST;
using namespace SST::Astra;
using namespace SST::Interfaces;

AstraWorkload::AstraWorkload(ComponentId_t id, Params& params) : Component(id) {

    out_ = new Output("", 1, 0, Output::STDOUT);
    dbg_ = new Output("[\@f:\@l:\@p:\@t] ", 1, 0, Output::STDERR);

    registerAsPrimaryComponent();
    primaryComponentDoNotEndSim();

    workloadConfig_        = params.find<std::string>("workloadConfig");
    systemConfig_          = params.find<std::string>("systemConfig");
    memoryConfig_          = params.find<std::string>("memoryConfig");
    commGroupConfig_       = params.find<std::string>("commGroupConfig", "empty");
    loggingConfig_         = params.find<std::string>("loggingConfig", "empty");
    numQueuesPerDim_       = params.find<int>("numQueuesPerDim",1);
    commScale_             = params.find<double>("commScale", 1.0);
    injectionScale_        = params.find<double>("injectionScale", 1.0);
    rendezvousProtocol_    = params.find<bool>("rendezvousProtocol", false);

    params.find_array<int>("logicalTopologyConfig", logicalDims_);
    numNPUs_ = 1;
    for (int x : logicalDims_) {
        numNPUs_ *= x;
    }
    queuesPerDim_ = std::vector<int>(logicalDims_.size(), numQueuesPerDim_);

    params.print_all_params(*dbg_);

    dbg_->debug(CALL_INFO, 1, 0, "AstraWorkload params\n");
    dbg_->debug(CALL_INFO, 1, 0, "  workloadConfig_: %s\n", workloadConfig_.c_str());
    dbg_->debug(CALL_INFO, 1, 0, "  systemConfig_: %s\n", systemConfig_.c_str());
    dbg_->debug(CALL_INFO, 1, 0, "  memoryConfig_: %s\n", memoryConfig_.c_str());
    dbg_->debug(CALL_INFO, 1, 0, "  commGroupConfig_: %s\n", commGroupConfig_.c_str());
    //dbg_->debug(CALL_INFO, 1, 0, "  logicalTopologyConfig_: %s\n", logicalTopologyConfig_.c_str());
    dbg_->debug(CALL_INFO, 1, 0, "  loggingConfig_: %s\n", loggingConfig_.c_str());
    dbg_->debug(CALL_INFO, 1, 0, "  numQueuesPerDim_: %d\n", numQueuesPerDim_);
    dbg_->debug(CALL_INFO, 1, 0, "  commScale_: %d\n", commScale_);
    dbg_->debug(CALL_INFO, 1, 0, "  injectionScale_: %lf\n", injectionScale_);
    dbg_->debug(CALL_INFO, 1, 0, "  rendezvousProtocol_: %lf\n", rendezvousProtocol_);

    /*
    clockHandler_ = new Clock::Handler<AstraWorkload, &AstraWorkload::clock>(this);
    time_ = registerClock(freq_, clockHandler_);
    */

    AstraSim::LoggerFactory::init(loggingConfig_);

    dbg_->debug(CALL_INFO, 1, 0, "AstraWorkload will create %d nics and systems\n", numNPUs_);

    dbg_->debug(CALL_INFO, 1, 0, "Creating Remote Memory\n");
    Analytical::AnalyticalRemoteMemory* mem_ =
         new Analytical::AnalyticalRemoteMemory(memoryConfig_);

    for (int i = 0; i < numNPUs_; i++) {

        dbg_->debug(CALL_INFO, 1, 0, "Loading nic %d\n", i);
        nics_.push_back( loadAnonymousSubComponent<AstraNIC>("astra.AstraNIC", "nic", i, ComponentInfo::SHARE_PORTS, params, i) );

        if (!nics_.back()) {
            out_->fatal(CALL_INFO, 1, "Failed to load AstraNIC %d\n", i);
        }

        dbg_->debug(CALL_INFO, 1, 0, "Creating system %d\n", i);
        systems_.push_back(new AstraSim::Sys(
                i, workloadConfig_, commGroupConfig_,
                systemConfig_, mem_, nics_.back()->getNetworkInterface(), logicalDims_,
                queuesPerDim_, injectionScale_, commScale_, rendezvousProtocol_));

    }

    dbg_->debug(CALL_INFO, 1, 0, "Done creating nic and systems\n");
    /*
    for (int i = 0; i < numNPUs_; i++) {
        linkControl_[i] = loadUserSubComponent<SST::Interfaces::SimpleNetwork>("linkControl" + std::to_string(i), ComponentInfo::SHARE_NONE, 1);
    }
    */
}

AstraWorkload::AstraWorkload() : Component() {}

bool AstraWorkload::clock(SimTime_t cycle) {
    return false;
}


SimTime_t AstraWorkload::getCurrentSimTimeNanoWrapper() {
    return getCurrentSimTimeNano();
}

AstraWorkload::~AstraWorkload()
{
    delete out_;
    delete dbg_;
}

