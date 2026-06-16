#pragma once

#include <sst/core/event.h>
#include <sst/core/output.h>
#include <sst/core/subcomponent.h>
#include <sst/core/interfaces/simpleNetwork.h>

#include "astraEvent.h"


namespace SST {
namespace Astra {

class AstraNIC : public SST::Interfaces::SimpleNetwork {

public:
    SST_ELI_REGISTER_SUBCOMPONENT(SST::Astra::AstraNIC, "astra", "AstraNIC", SST_ELI_ELEMENT_VERSION(1,0,0), "Network interface for the AstraController", SST::Interfaces::SimpleNetwork)
    SST_ELI_DOCUMENT_PARAMS()
    SST_ELI_DOCUMENT_PORTS( { "port", "Link to network", { "astra.AstraEvent" } } )
    SST_ELI_DOCUMENT_SUBCOMPONENT_SLOTS( { "linkcontrol", "Network interface"} )

    AstraNIC(ComponentId_t id, Params &params, TimeConverter tc);
    AstraNIC(ComponentId_t id); //TODO do i need this?

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

    void sendUntimedData(Request *req) override;
    Request* recvUntimedData() override;
    bool send(Request* req, int vn) override;
    Request* recv(int* vn) override;

    bool spaceToSend(int vn, int num_bits) override;
    bool requestToReceive(int vn) override;
    void setNotifyOnReceive(HandlerBase* functor) override;
    void setNotifyOnSendr(HandlerBase* functor) override;
    bool isNetworkInitialized() override;
    nid_t getEndpointID() override;
    const UnitAlgebra& getLinkBW() override {};


private:

    SST::Interfaces::SimpleNetwork *linkControl_;

    // Event queues
    std::queue<SST::Interfaces::SimpleNetwork::Request*> sendQueue; // Queue of events waiting to be sent on clock

    // Clocks
    Clock::HandlerBase* clockHandler_;
    TimeConverter clockTC_;

}; // class AstraNIC
} // namespace Astra
} // namespace SST
