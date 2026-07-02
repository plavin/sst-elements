#include <sst/core/sst_config.h>
#include "sst/elements/astra/astraNIC.h"

//using namespace SST;
using namespace SST::Interfaces;

namespace SST { namespace Astra {

AstraNIC::AstraNIC(ComponentId_t id, Params &params, int nicID) : SubComponent(id), nicID_(nicID) {

    out_ = new Output("", 1, 0, Output::STDOUT);
    dbg_ = new Output("[\@f:\@l:\@p:\@t] ", 1, 0, Output::STDOUT);

    // In Astra simulations, the only primary components are the AstraNICs. The AstraNetworkAPI will
    // notify us when the simulation can be ended.
    registerAsPrimaryComponent();
    primaryComponentDoNotEndSim();

    // NIC Params
    mtu_ = params.find<int>("mtu", "1500"); //bytes

    // Link params
    networkInterface_ = new AstraNetworkInterface(nicID_, *this);
    std::string lctype = params.find<std::string>("linkcontrol", "merlin.reorderlinkcontrol");
    Params lcparams;
    lcparams.insert("link_bw", params.find<std::string>("network_bw", "100Gb/s"));
    lcparams.insert("input_buf_size", params.find<std::string>("network_input_buffer_size", "10kB"));
    lcparams.insert("output_buf_size", params.find<std::string>("network_output_buffer_size", "10kB"));

    freq_ = params.find<std::string>("frequency", "2.0GHz");

    // TODO: get from params?
    std::string portName = "port" + std::to_string(nicID_);

    lcparams.insert("port_name", portName);
    dbg_->debug(CALL_INFO, 1, 0, "Loading linkController:\n");
    dbg_->debug(CALL_INFO, 1, 0, "  nicID_: %d\n", nicID);
    dbg_->debug(CALL_INFO, 1, 0, "  type: %s\n", lctype.c_str());
    dbg_->debug(CALL_INFO, 1, 0, "  portname: %s\n", portName.c_str());

    linkControl_ = loadAnonymousSubComponent<SST::Interfaces::SimpleNetwork>(lctype, portName, 0, ComponentInfo::SHARE_PORTS | ComponentInfo::INSERT_STATS, lcparams, 1);
    if (!linkControl_) out_->fatal(CALL_INFO, 1, "Failed to load linkcontroller\n");
    linkControl_->setNotifyOnSend(new SimpleNetwork::Handler<AstraNIC, &AstraNIC::handleSend>(this));
    linkControl_->setNotifyOnReceive(new SimpleNetwork::Handler<AstraNIC, &AstraNIC::handleRecv>(this));

    selfLink_ = configureSelfLink("self", "1GHz" /* ns */, new Event::Handler<AstraNIC, &AstraNIC::handleSimSchedule>(this));
    if (!selfLink_) out_->fatal(CALL_INFO, 1, "Failed to configure selfLink_\n");
};

AstraNetworkInterface* AstraNIC::getNetworkInterface() {
    return networkInterface_;
}

void AstraNIC::init(unsigned int phase) {
    linkControl_->init(phase);
}

void AstraNIC::setup() {
    linkControl_->setup();
}

void AstraNIC::complete(unsigned int phase) {
    linkControl_->complete(phase);
}

void AstraNIC::finish() {
    linkControl_->finish();
}

void AstraNIC::handleSimSchedule(Event* ev) {
    dbg_->debug(CALL_INFO, 1, 0, "nicID=%d handleSimSchedule %s\n", nicID_);
    auto ae = static_cast<AstraEvent*>(ev);
    ae->msg_handler_(ae->fun_arg_);
    delete(ae);
}

// Push as many sends across the link as possible. Called by sim_send anytime a new message
// is received and by the linkController anytime it sends a message to the network (setNotifyOnSend).
bool AstraNIC::handleSend(int) {
    dbg_->debug(CALL_INFO, 1, 0, "nicID=%d Send queue size: %d\n", nicID_, sendQueue.size());

    int sendCount = 0;
    while(!sendQueue.empty()) {
        SimpleNetwork::Request* head = sendQueue.front();
        if (!linkControl_->spaceToSend(0, head->size_in_bits)) {
            dbg_->debug(CALL_INFO, 1, 0, "No space to send!\n");
            break;
        } else if (!linkControl_->send(head, 0)){
            dbg_->debug(CALL_INFO, 1, 0, "Failed to send!\n");
            break;
        } else {
            sendQueue.pop();
            sendCount += 1;
        }
    }

    dbg_->debug(CALL_INFO, 1, 0, "nicID=%d Sent %d events\n", nicID_, sendCount);

    return true; // Keep this handler registered
}

// Called when a packet is received from the network
bool AstraNIC::handleRecv(int) {
    dbg_->debug(CALL_INFO, 1, 0, "nicID=%d handleRecv called\n", nicID_);
    SST::Interfaces::SimpleNetwork::Request* req = linkControl_->recv(0);
    AstraEvent* ae = static_cast<AstraEvent*>(req->takePayload());

    // This is the end of a message
    if (ae->tail_) {

        // Notify AstraSim that the Send has completed
        ae->msg_handler_(ae->fun_arg_);

        MsgKey mk{req->src, req->dest, ae->tag_};
        auto it = msgMap_.find(mk);
        if (it != msgMap_.end()) {
            // The matching Recv has already posted. Call it's handler and delete it.
            CallbackHolder& cb = it->second;
            cb.invoke();
            msgMap_.erase(it);
        } else {
            // The matching Recv has not yet posted. Record that the send is finished.
            msgMap_[mk] = CallbackHolder{};
        }
    }
    delete(ae);
    delete(req);

    return true; // Keep this handler registered
}

/*********************************************************/
/*                   AstraNetworkAPI                     */
/*********************************************************/

// These functions implement AstraSim::AstraNetworkAPI.
// AstraNetworkInterface inherits from AstraNetworkAPI and
// serves as a thin layer so that we can
// avoid multiple inheritance in this class (AstraNIC).

AstraSim::timespec_t AstraNIC::sim_get_time() {
    AstraSim::timespec_t ts;
    ts.time_res = AstraSim::NS;
    ts.time_val = getCurrentSimTimeNano();
    return ts;
}

// Receive messages from AstraSim and packetize them
// Currently, we ignore `buffer`, `type` and `request`.
int AstraNIC::sim_send(void* buffer,
				 uint64_t count,
				 int type,
				 int dst,
				 int tag,
				 AstraSim::sim_request* request,
				 void (*msg_handler)(void* fun_arg),
				 void* fun_arg)
{

    dbg_->debug(CALL_INFO, 1, 0, "nicID=%d sim_send\n", nicID_);

    int num_packets = (count / mtu_) + ((count % mtu_) != 0);
    int msg_size_rem = count;
    for (int i = 0; i < num_packets; i++) {
        auto ae = new AstraEvent();
        ae->tag_ = tag;

        auto req = new SimpleNetwork::Request();
        req->src = nicID_;
        req->dest = dst;

        if (i == (num_packets - 1)) {
            ae->msg_handler_ = msg_handler;
            ae->fun_arg_ = fun_arg;
            ae->tail_ = true;
            req->size_in_bits = msg_size_rem * 8;
        } else {
            ae->tail_ = false;
            req->size_in_bits = mtu_ * 8;
            msg_size_rem -= mtu_;
        }

        req->givePayload(ae);

        sendQueue.push(req);
    }

    dbg_->debug(CALL_INFO, 1, 0, "Pushed %d packets\n", num_packets);
    handleSend(0); // If the queue was empty, we need to make sure this gets called
    return 0;
}

int AstraNIC::sim_recv(void* msg,
        uint64_t msg_size,
        int type,
        int src,
        int tag,
        AstraSim::sim_request* request,
        void (*msg_handler)(void* fun_arg),
        void* fun_arg) {

    MsgKey mk{src, nicID_, tag};

    dbg_->debug(CALL_INFO, 1, 0, "nicID=%d\n", nicID_);

    auto it = msgMap_.find(mk);
    if (it != msgMap_.end()) {
        // Match Send already completed. Notify AstraSim that the recieve has completed.
        msg_handler(fun_arg);
        msgMap_.erase(it);
    } else{
        // Matching send not yet completed. Record that this recive has posted.
        msgMap_[mk] = CallbackHolder{msg_handler, fun_arg};
    }
    return 0;

}

void AstraNIC::sim_schedule(AstraSim::timespec_t delta,
                void (*fun_ptr)(void* fun_arg),
                void* fun_arg) {
    dbg_->debug(CALL_INFO, 1, 0, "nicID=%d sim_schedule\n", nicID_);
    auto ae = new AstraEvent();
    ae->msg_handler_ = fun_ptr;
    ae->fun_arg_ = fun_arg;
    selfLink_->send(delta.time_val, ae); // It seems ASTRA-sim always uses nanoseconds. We have configured the selflink to be the same

}

void AstraNIC::sim_notify_finished() {
    if (msgMap_.size() != 0) {
        out_->output(CALL_INFO, "WARNING: sim_notify_finished called with non-empty msgMap\n");
    }
    primaryComponentOKToEndSim();
}

} // namespace Astra
} // namespace SST

