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

    networkInterface_ = new AstraNetworkInterface(nicID_, *this);
    std::string lctype = params.find<std::string>("linkcontrol", "merlin.linkcontrol");
    Params lcparams;
    lcparams.insert("link_bw", params.find<std::string>("network_bw", "80GiB/s"));
    lcparams.insert("in_buf_size", params.find<std::string>("network_input_buffer_size", "1KiB"));
    lcparams.insert("out_buf_size", params.find<std::string>("network_output_buffer_size", "1KiB"));

    freq_ = params.find<std::string>("frequency", "2.0GHz");
    clockHandler_ = new Clock::Handler<AstraNIC, &AstraNIC::tick>(this);
    registerClock(freq_, clockHandler_);
    isClocked_ = true;

    // TODO: get from params?
    std::string portName = "port" + std::to_string(nicID_);

    lcparams.insert("port_name", portName);
    dbg_->debug(CALL_INFO, 1, 0, "Loading linkController:\n");
    dbg_->debug(CALL_INFO, 1, 0, "  nicID_: %d\n", nicID);
    dbg_->debug(CALL_INFO, 1, 0, "  type: %s\n", lctype.c_str());
    dbg_->debug(CALL_INFO, 1, 0, "  portname: %s\n", portName.c_str());
    linkControl_ = loadAnonymousSubComponent<SST::Interfaces::SimpleNetwork>(lctype, portName, 0, ComponentInfo::SHARE_PORTS | ComponentInfo::INSERT_STATS, lcparams, 1);

    if (!linkControl_) out_->fatal(CALL_INFO, 1, "Failed to load linkcontroller\n");
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

bool AstraNIC::tick(SimTime_t cycle) {
    bool disableClock = false;

    dbg_->debug(CALL_INFO, 1, 0, "nicID=%d Send queue size: %d\n", nicID_, sendQueue.size());

    //drain send queue
    int sendCount = 0;
    while(!sendQueue.empty()) {
        auto head = sendQueue.front();
        if (linkControl_->spaceToSend(0, head->size_in_bits) && linkControl_->send(head, 0)) {
            sendQueue.pop();
            sendCount += 1;
        } else {
            break;
        }
    }

    dbg_->debug(CALL_INFO, 1, 0, "nicID=%d Sent %d events\n", nicID_, sendCount);
    //return false;
    // TODO - continue to evaluate if this changes the timings. seems OK for now.
    if (sendQueue.empty()) {
        disableClock = true;
        isClocked_ = false;
    }
    return disableClock;
}

void AstraNIC::handleSimSchedule(Event* ev) {
    dbg_->debug(CALL_INFO, 1, 0, "nicID=%d handleSimSchedule %s\n", nicID_);
    auto ae = static_cast<AstraEvent*>(ev);
    ae->msg_handler_(ae->fun_arg_);
    // The event should be delayed when it is put on the Link. We may call it immediately
    if (!isClocked_) {
        // TODO - is this needed?
        reregisterClock(freq_, clockHandler_);
        isClocked_ = true;
    }
}

// Notifies us that a send across the network has completed
bool AstraNIC::handleRecv(int) {
    dbg_->debug(CALL_INFO, 1, 0, "nicID=%d handleRecv called\n", nicID_);
    SST::Interfaces::SimpleNetwork::Request* req = linkControl_->recv(0);
    AstraEvent* ae = static_cast<AstraEvent*>(req->takePayload());

    // Notify AstraSim::Sys that the send has completed
    ae->msg_handler_(ae->fun_arg_);

    MsgKey mk{ae->src_, ae->dst_, ae->tag_};
    auto it = msgMap_.find(mk);
    if (it != msgMap_.end()) {
        // Recv already posted
        CallbackHolder& cb = it->second;
        cb.invoke();
        msgMap_.erase(it);
    } else {
        // Recv not yet posted
        msgMap_[mk] = CallbackHolder{};
    }

    if (!isClocked_) {
        // TODO - is this needed? - Answer may depend on if we get a send or a recv and whether we already have the other side
        reregisterClock(freq_, clockHandler_);
        isClocked_ = true;
    }

    delete(ae);
    return true;
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

    auto ae = new SST::Astra::AstraEvent();
    ae->buffer_ = buffer; //TODO - copy? free original?
    ae->count_ = count;
    ae->type_ = type;
    ae->src_ = nicID_;
    ae->dst_ = dst;
    ae->tag_ = tag;
    ae->request_ = request; // TODO - what do we do with this?
    ae->msg_handler_ = msg_handler;
    ae->fun_arg_ = fun_arg;

    auto req = new SimpleNetwork::Request();
    req->src = nicID_;
    req->dest = ae->dst_;
    req->givePayload(ae);
    sendQueue.push(req);
    if (!isClocked_) {
        reregisterClock(freq_,clockHandler_);
        isClocked_ = true;
    }
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

    dbg_->debug(CALL_INFO, 1, 0, "nicID=%d: MsgKey: %s\n", nicID_, mk.to_string());

    auto it = msgMap_.find(mk);
    if (it != msgMap_.end()) {
        //Send already completed
        msg_handler(fun_arg);
        msgMap_.erase(it);
    } else{
        //Send not yet completed
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
    //TODO - can we be sure that all sends and recieves are done when this is called? Need to investigate why ns3 frontend has that tracker
    assert(msgMap_.size() == 0);
    if (msgMap_.size() != 0) {
        out_->output(CALL_INFO, "WARNING: sim_notify_finished called with non-empty msgMap\n");
    }
    primaryComponentOKToEndSim();
}

} // namespace Astra
} // namespace SST

