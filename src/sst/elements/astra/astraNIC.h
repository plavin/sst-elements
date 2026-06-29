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
    SST_ELI_DOCUMENT_PORTS( {"self", "Self link for scheduling sim_schedule calls", { "astra.AstraEvent" }} )

    AstraNIC(ComponentId_t id, Params &params, int nicID);
    ~AstraNIC() { }

    AstraNetworkInterface *getNetworkInterface();

    bool tick(SimTime_t cycle);
    void handleSimSchedule(Event* ev);
    bool handleRecv(int);

    void init(unsigned int phase) override;
    void setup() override;
    void complete(unsigned int phase) override;
    void finish() override;

    bool isClocked();

    // AstraNetworkAPI
    void sim_schedule(AstraSim::timespec_t delta,
            void (*fun_ptr)(void* fun_arg),
            void* fun_arg);
    AstraSim::timespec_t sim_get_time();
    void sim_notify_finished();
    int sim_send(void* buffer,
					 uint64_t count,
					 int type,
					 int dst,
					 int tag,
					 AstraSim::sim_request* request,
					 void (*msg_handler)(void* fun_arg),
					 void* fun_arg); //TODO - mark override if we end up doing multiple inheritance




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

    SST::Link* selfLink_;

}; // class AstraNIC
} // namespace Astra
} // namespace SST
