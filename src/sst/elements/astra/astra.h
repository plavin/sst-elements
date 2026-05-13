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

#ifndef _ASTRA_H
#define _ASTRA_H

/*
 * TODO: Add explanation
 */

#include <string>
#include <vector>
#include <sst/core/component.h>
#include <sst/core/link.h>
#include "astra-sim/common/AstraNetworkAPI.hh"
#include "astra-sim/system/Sys.hh"

namespace SST {
namespace astra {

class astraNetworkBridge : public SST::Component
{
public:
    SST_ELI_REGISTER_COMPONENT(
        astraNetworkBridge,
        "astra",
        "astraNetworkBridge",
        SST_ELI_ELEMENT_VERSION(1,0,0),
        "ASTRA-sim network backend for SST",
        COMPONENT_CATEGORY_NETWORK
    )

	SST_ELI_DOCUMENT_PARAMS(
        {"workloadConfig",        "Workload config file",                  NULL    },
        {"systemConfig",          "System config file",                    NULL    },
        {"networkConfig",         "Network config file",                   NULL    },
        {"memoryConfig",          "Remote memory config file",             NULL    },
        {"commGroupConfig",       "Communicator group config file",        "empty" },
        {"logicalTopologyConfig", "Logical topology config file",          NULL    },
        {"loggingConfig",         "Logging config file",                   "empty" },
        {"numQueuesPerDim",       "Number of queues per dimension",        "1"     },
        {"commScale",             "Communication scale",                   "1.0"   },
        {"injectionScale",        "Injection scale",                       "1.0"   },
        {"rendezvousProtocol",    "Whether to enable rendezvous protocol", "false" },
    )

    SST_ELI_DOCUMENT_PORTS(
        { "testPort", "Link to Merlin", { "simpleNetwork", ""} }
    )

    SST_ELI_DOCUMENT_STATISTICS( )

    astraNetworkBridge(SST::ComponentId_t id, SST::Params& params);
    astraNetworkBridge();
    ~astraNetworkBridge();

    NotSerializable(SST::astra::astraNetworkBridge)

private:
    SST::Output* out;

    std::string workloadConfig_;
    std::string systemConfig_;
    std::string networkConfig_;
    std::string memoryConfig_;
    std::string commGroupConfig_;
    std::string logicalTopologyConfig_;
    std::string loggingConfig_;
    int numQueuesPerDim_;
    double commScale_;
    double injectionScale_;
    bool rendezvousProtocol_;

    int numNPUs_;
    std::vector<int> logicalDims_;
    std::vector<int> queuesPerDim_;

};

}
}
#endif /* _ASTRA_H */
