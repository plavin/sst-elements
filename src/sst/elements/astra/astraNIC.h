#pragma once

#include <sst/core/event.h>
#include <sst/core/output.h>
#include <sst/core/subcomponent.h>
#include <sst/core/interfaces/simpleNetwork.h>

#include "astraNetworkInterface.h"
#include "astraEvent.h"


namespace SST {
namespace Astra {

class AstraNIC : public SubComponent {

public:
    SST_ELI_REGISTER_SUBCOMPONENT(SST::Astra::AstraNIC,
            "astra",
            "AstraNIC",
            SST_ELI_ELEMENT_VERSION(1,0,0),
            "Network interface for the AstraController",
            SST::Astra::AstraNIC)
    SST_ELI_REGISTER_SUBCOMPONENT_API(SST::Astra::AstraNIC, int)
    SST_ELI_DOCUMENT_PARAMS()
    SST_ELI_DOCUMENT_SUBCOMPONENT_SLOTS( { "linkcontrol", "Network interface"} )

    AstraNIC(ComponentId_t id, Params &params, int nicID);
    ~AstraNIC() { }

    AstraNetworkInterface *getNetworkInterface();
    SimTime_t getCurrentSimTimeNanoWrapper();

    void send(AstraEvent *ev);

    bool tick(SimTime_t cycle);

    void init(unsigned int phase) override;
    void setup() override;
    void complete(unsigned int phase) override;
    void finish() override;

    bool isClocked();

private:

    SST::Interfaces::SimpleNetwork *linkControl_;

    SST::Output* out_;
    SST::Output* dbg_;

    // Event queues
    std::queue<SST::Interfaces::SimpleNetwork::Request*> sendQueue; // Queue of events waiting to be sent on clock

    // AstraSim simulator object
    AstraNetworkInterface *networkInterface_;

    std::string freq_;

    // Clocks
    Clock::HandlerBase* clockHandler_;
    TimeConverter time_;
    int nicID_;
    bool isClocked_;

}; // class AstraNIC
} // namespace Astra
} // namespace SST
