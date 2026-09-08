#pragma once

#include <tuple>

#include <sst/core/event.h>
#include <sst/core/output.h>
#include <sst/core/subcomponent.h>
#include <sst/core/interfaces/simpleNetwork.h>

#include "astraNetworkInterface.h"
#include "astraEvent.h"

#include "msgKey.h"

namespace SST {
namespace Astra {

class CallbackHolder {
public:
    using MsgHandler = void (*)(void*);

    CallbackHolder() = default;

    CallbackHolder(MsgHandler handler, void* arg)
        : msg_handler(handler), fun_arg(arg) {}

    void invoke() const {
        if (msg_handler) {
            msg_handler(fun_arg);
        }
    }

private:
    MsgHandler msg_handler = nullptr;
    void* fun_arg = nullptr;
};

class AstraNIC : public SubComponent {

public:
    SST_ELI_REGISTER_SUBCOMPONENT(SST::Astra::AstraNIC,
            "astra",
            "AstraNIC",
            SST_ELI_ELEMENT_VERSION(1,0,0),
            "Network interface for the AstraController",
            SST::Astra::AstraNIC)
    SST_ELI_REGISTER_SUBCOMPONENT_API(SST::Astra::AstraNIC, int)
    SST_ELI_DOCUMENT_PARAMS(
            { "mtu", "Maximum packet size in bytes", "1500"},
            { "trace", "Generate a trace of when sends and recieves post/finish", "false"},
            { "trace_prefix", "Trace files will be named <trace_prefix>_<nicID>.txt", "astranic_trace_"}
    )
    SST_ELI_DOCUMENT_SUBCOMPONENT_SLOTS( { "linkcontrol", "Network interface"} )
    SST_ELI_DOCUMENT_PORTS( {"self", "Self link for scheduling sim_schedule calls", { "astra.AstraEvent" }} )
    SST_ELI_DOCUMENT_STATISTICS(
        {"astra_messages_sent", "Total AstraSim messages sent", "messages", 1},
        {"astra_messages_received", "Total AstraSim messages received", "messages", 1},
    )

    AstraNIC(ComponentId_t id, Params &params, int nicID);
    ~AstraNIC() { }

    AstraNetworkInterface *getNetworkInterface();

    void handleSimSchedule(Event* ev);
    bool handleSend(int);
    bool handleRecv(int);

    void init(unsigned int phase) override;
    void setup() override;
    void complete(unsigned int phase) override;
    void finish() override;

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
    int sim_recv(void* msg,
         uint64_t msg_size,
         int type,
         int src,
         int tag,
         AstraSim::sim_request* request,
         void (*msg_handler)(void* fun_arg),
         void* fun_arg); //TODO - mark override if we end up doing multiple inheritance




private:

    SST::Interfaces::SimpleNetwork *linkControl_;

    SST::Output* out_;

    // Event queues
    std::queue<SST::Interfaces::SimpleNetwork::Request*> sendQueue; // Queue of packets waiting to be sent across the link
    std::queue<SST::Interfaces::SimpleNetwork::Request*> recvQueue; // Queue of packets waiting to be processed

    // AstraSim simulator object
    AstraNetworkInterface *networkInterface_;

    // Clocks
    Clock::HandlerBase* clockHandler_;
    TimeConverter time_;
    int nicID_;

    // Params
    int mtu_;
    bool trace_;
    SST::Output* trace_file_;

    SST::Link* selfLink_;

    // Holds track events so we know when to call recv msgHandlers
    std::unordered_map<MsgKey, CallbackHolder, MsgKeyHash> msgMap_;

    Statistic<uint64_t>* statMessagesSent;
    Statistic<uint64_t>* statMessagesReceived;

}; // class AstraNIC
} // namespace Astra
} // namespace SST
