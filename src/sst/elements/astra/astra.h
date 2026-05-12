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

#include <sst/core/component.h>
#include <sst/core/link.h>

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
        { "testParam", "test parameter" }
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


};

}
}
#endif /* _ASTRA_H */
