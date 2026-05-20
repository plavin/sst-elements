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
    SST_ELI_DOCUMENT_PORTS( { "port", "Link to network", { "astra.AstraEvent" } } )
    SST_ELI_DOCUMENT_SUBCOMPONENT_SLOTS( { "linkcontrol", "Network interface"} )

    AstraNIC(ComponentID_t id, Params &params, TimeConverter tc);

    ~AstraNIC() { }

    void send(AstraEvent* ev);
    AstraEvent* recv();
    bool isClocked() { return true; } //TODO??

    // Callback to notify NIC when linkcontrol has recived a message
    bool recvNotify(int);

    bool clock(SimTime_t cycle);

    size_t getSizeInBits(AstraEvent *ev);

    void init(unsigned int phase) override;
    void setup() override { linkControl_->setup(); }
    void complete(unsigned int phase) override;
    void finish() override { linkControl_->finish(); }

private:

    SST::Interfaces::SimpletNetwork *linkControl_;

    // Event queues
    std::queue<SST::Interfaces::SimpleNetwork::Request*> sendQueue; // Queue of events waiting to be sent on clock

    // Clocks
    Clock::HanderBase* clockHandler_;
    TimeConverter clockTC_;

}; // class AstraNIC
} // namespace Astra
} // namespace SST
