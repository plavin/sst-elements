#include <sst/core/sst_config.h>
#include "sst/elements/astra/astraNIC.h"

//using namespace SST;
using namespace SST::Interfaces;

namespace SST { namespace Astra {

AstraNIC::AstraNIC(ComponentId_t id, Params &params, int nicID) : SubComponent(id), nicID_(nicID) {
//AstraNIC::AstraNIC(ComponentId_t id, Params &params) : SubComponent(id) {
    out_ = new Output("", 1, 0, Output::STDOUT);
    dbg_ = new Output("[\@f:\@l:\@p:\@t] ", 1, 0, Output::STDERR);

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

//AstraNIC::AstraNIC(ComponentId_t id) : SubComponent(id) { }

AstraNetworkInterface* AstraNIC::getNetworkInterface() {
    return networkInterface_;
}

AstraSim::timespec_t AstraNIC::sim_get_time() {
    AstraSim::timespec_t ts;
    ts.time_res = AstraSim::NS;
    ts.time_val = getCurrentSimTimeNano();
    return ts;
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

bool AstraNIC::isClocked() {
    return isClocked_;
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

    if (sendQueue.empty()) {
        disableClock = true;
        isClocked_ = false;
    }
    return disableClock;
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
    dbg_->debug(CALL_INFO, 1, 0, "nicID=%d Received send event from AstraNetworkInterface\n", nicID_);

    auto ae = new SST::Astra::AstraEvent();
    ae->dst_ = dst;
    //TODO - fill in reset of ae feilds

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

void AstraNIC::sim_schedule(AstraSim::timespec_t delta,
                void (*fun_ptr)(void* fun_arg),
                void* fun_arg) {
    auto ae = new AstraEvent();
    ae->msg_handler_ = fun_ptr;
    ae->fun_arg_ = fun_arg;
    selfLink_->send(delta.time_val, ae); // It seems ASTRA-sim always uses nanoseconds. We have configured the selflink to be the same

}

void AstraNIC::sim_notify_finished() {
    //TODO - can we be sure that all sends and recieves are done when this is called? Need to investigate why ns3 frontend has that tracker
    primaryComponentOKToEndSim();
}


void AstraNIC::handleSimSchedule(Event* ev) {
    auto ae = static_cast<AstraEvent*>(ev);
    ae->msg_handler_(ae->fun_arg_);
    // The event should be delayed when it is put on the Link. We may call it immediately
    if (!isClocked_) {
        // TODO - is this needed?
        reregisterClock(freq_, clockHandler_);
    }
}

bool AstraNIC::handleRecv(int) {
    SST::Interfaces::SimpleNetwork::Request* req = linkControl_->recv(0);
    AstraEvent* ae = static_cast<AstraEvent*>(req->takePayload());
    ae->msg_handler_(ae->fun_arg_);
    /*
    auto sn = static_cast<
    auto ae = static_cast<AstraEvent*>(ev);
    */
    //ae->msg_handler_(ae->fun_arg_);
    // TODO test to see if the matching recv has posted
    // TODO call appropriate handlers
    // TODO figure out what to do if recv comes first - how to store it
    //ae->msg_handler_(ae->fun_arg_);
    if (!isClocked_) {
        // TODO - is this needed? - Answer may depend on if we get a send or a recv and whether we already have the other side
        reregisterClock(freq_, clockHandler_);
    }
    return true;
}

} // namespace Astra
} // namespace SST

