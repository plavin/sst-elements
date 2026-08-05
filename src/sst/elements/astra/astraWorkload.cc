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

    out_ = new Output("[\@f:\@l:\@p:\@t] ", 1, 0, Output::STDERR);

    workloadConfig_        = params.find<std::string>("workloadConfig");
    systemConfig_          = params.find<std::string>("systemConfig");
    memoryConfig_          = params.find<std::string>("memoryConfig");
    commGroupConfig_       = params.find<std::string>("commGroupConfig", "empty");
    loggingConfig_         = params.find<std::string>("loggingConfig", "empty");
    numNPUs_               = params.find<int>("numNPUs");
    numQueuesPerDim_       = params.find<int>("numQueuesPerDim",1);
    commScale_             = params.find<double>("commScale", 1.0);
    injectionScale_        = params.find<double>("injectionScale", 1.0);
    rendezvousProtocol_    = params.find<bool>("rendezvousProtocol", false);

    // TODO: What am I actually supposed to put in logicalDims?
    logicalDims_.push_back(numNPUs_);
    queuesPerDim_ = std::vector<int>(logicalDims_.size(), numQueuesPerDim_);

    out_->debug(CALL_INFO, 1, 0, "AstraWorkload params\n");
    out_->debug(CALL_INFO, 1, 0, "  workloadConfig_: %s\n", workloadConfig_.c_str());
    out_->debug(CALL_INFO, 1, 0, "  systemConfig_: %s\n", systemConfig_.c_str());
    out_->debug(CALL_INFO, 1, 0, "  memoryConfig_: %s\n", memoryConfig_.c_str());
    out_->debug(CALL_INFO, 1, 0, "  commGroupConfig_: %s\n", commGroupConfig_.c_str());
    out_->debug(CALL_INFO, 1, 0, "  loggingConfig_: %s\n", loggingConfig_.c_str());
    out_->debug(CALL_INFO, 1, 0, "  numQueuesPerDim_: %d\n", numQueuesPerDim_);
    out_->debug(CALL_INFO, 1, 0, "  commScale_: %d\n", commScale_);
    out_->debug(CALL_INFO, 1, 0, "  injectionScale_: %lf\n", injectionScale_);
    out_->debug(CALL_INFO, 1, 0, "  rendezvousProtocol_: %d\n", rendezvousProtocol_);

    AstraSim::LoggerFactory::init(loggingConfig_);

    out_->debug(CALL_INFO, 1, 0, "Creating Remote Memory\n");
    Analytical::AnalyticalRemoteMemory* mem_ =
         new Analytical::AnalyticalRemoteMemory(memoryConfig_);

    out_->debug(CALL_INFO, 1, 0, "AstraWorkload will create %d nics and systems\n", numNPUs_);

    // First, see if the user loaded the NICs. If so, it is an error to load any number other than numNPU_ nics
    // Please number them sequentially because I don't know what happens if you don't
    SubComponentSlotInfo *lists = getSubComponentSlotInfo("nic");
    if (lists) {
        int foundNICs = 0;
        for (int i = 0; i < lists->getMaxPopulatedSlotNumber()+1; i++) {
            if (lists->isPopulated(i)) {
                out_->flush();
                foundNICs++;
                nics_.push_back( lists->create<AstraNIC>(i, ComponentInfo::SHARE_PORTS, i) ); // TODO - AstraNIC needs its own ports
                if (!nics_.back()) {
                    out_->fatal(CALL_INFO, 1, "Failed to load AstraNIC %d\n", i);
                }
            }
        }
        if (numNPUs_ != foundNICs) {
            out_->fatal(CALL_INFO, 1, "Expected %d nics, got %d\n", numNPUs_, foundNICs);
        }
    } else {
        for (int i = 0; i < numNPUs_; i++) {

            out_->debug(CALL_INFO, 1, 0, "Loading nic %d\n", i);
            nics_.push_back( loadAnonymousSubComponent<AstraNIC>("astra.AstraNIC", "nic", i, ComponentInfo::SHARE_PORTS, params, i) );

            if (!nics_.back()) {
                out_->fatal(CALL_INFO, 1, "Failed to load AstraNIC %d\n", i);
            }
        }
    }

    for (int i = 0; i < numNPUs_; ++i) {
        systems_.push_back(new AstraSim::Sys(
                i, workloadConfig_, commGroupConfig_,
                systemConfig_, mem_, nics_[i]->getNetworkInterface(), logicalDims_,
                queuesPerDim_, injectionScale_, commScale_, rendezvousProtocol_));
        out_->debug(CALL_INFO, 1, 0, "Creating system %d\n", i);

    }

    out_->debug(CALL_INFO, 1, 0, "Done creating nic and systems\n");
}

AstraWorkload::AstraWorkload() : Component() {}

AstraWorkload::~AstraWorkload()
{
    delete out_;
}

void AstraWorkload::init(unsigned int phase) {
	for (auto& nic : nics_)
		nic->init(phase);
}
void AstraWorkload::setup() {
	for (auto& nic : nics_)
		nic->setup();

    // Kick off ASTRA-sim
    for (auto& system: systems_)
        system->workload->fire();
}
void AstraWorkload::complete(unsigned int phase) {
	for (auto& nic : nics_)
		nic->complete(phase);
}
void AstraWorkload::finish() {
	for (auto& nic : nics_)
		nic->finish();
}

