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

#ifndef _H_DYNAMORIO_FRONTEND
#define _H_DYNAMORIO_FRONTEND

#include <sst/core/sst_config.h>
#include <sst/core/component.h>
#include <sst/core/params.h>
#include <sst/core/interprocess/mmapparent.h>

#include <stdint.h>
#include <unistd.h>

#include <map>
#include <string>
#include <vector>

#include "arielfrontendcommon.h"
#include "ariel_shmem.h"

namespace SST {
namespace ArielComponent {

#define STRINGIZE(input) #input

class DynamoRIOFrontend : public ArielFrontendCommon {
public:
    SST_ELI_REGISTER_SUBCOMPONENT(DynamoRIOFrontend, "ariel", "frontend.dynamorio", SST_ELI_ELEMENT_VERSION(1,0,0),
        "Ariel frontend for dynamic tracing using DynamoRIO", SST::ArielComponent::ArielFrontend)

    SST_ELI_DOCUMENT_PARAMS(
        {"launcher", "Specify the launcher to be used for instrumentation, default is path to drrun", STRINGIZE(DYNAMORIO_EXECUTABLE)},
        {"launchparamcount", "Number of parameters supplied for the launch tool", "0"},
        {"launchparam%(launchparamcount)d", "Set the parameter to the launcher", ""},
        {"arieltool", "Path to the Ariel DynamoRIO client shared library", ""},
        {"writepayloadtrace", "Trace write payloads and put real memory contents into the memory system", "0"})

    DynamoRIOFrontend(ComponentId_t id, Params& params, uint32_t cores, uint32_t qSize, uint32_t memPool);
    ~DynamoRIOFrontend();

    void emergencyShutdown() override;

private:
    SST::Core::Interprocess::MMAPParent<ArielTunnel>* tunnelmgr;

    std::string applauncher;
    uint32_t launch_param_count;
    std::vector<std::string> launch_params;
    std::string arieltool;
    int writepayloadtrace;

    int forkChildProcess(const char* app, char** args, std::map<std::string, std::string>& app_env, ariel_redirect_info_t redirect_info) override;
    void setForkArguments() override;
    void parseDynamoRIOParams(Params& params);
};

} // namespace ArielComponent
} // namespace SST

#endif // _H_DYNAMORIO_FRONTEND
