#pragma once


#include <sst/core/event.h>
#include <sst/core/output.h>
#include <sst/core/subcomponent.h>
#include <sst/core/interfaces/simpleNetwork.h>


namespace SST {
namespace Astra {

class AstraNIC : public SST::SubComponent {

public:
    SST_ELI_REGISTER_SUBCOMPONENT(SST::Astra::AstraNIC, "astra", "AstraNIC", SST_ELI_ELEMENT_VERSION(1,0,0), "Network interface for the AstraController", SST::Astra::AstraNIC)
    SST_ELI_DOCUMENT_PARAMS()
    SST_ELI_DOCUMENT_PORTS( 
    SST_ELI_DOCUMENT_SUBCOMPONENT_SLOTS( { "linkcontrol", "Network interface"} )

}; // class AstraNIC
} // namespace Astra
} // namespace SST
