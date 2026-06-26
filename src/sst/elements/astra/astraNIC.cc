#include <sst/core/sst_config.h>
#include "sst/elements/astra/astraNIC.h"

//using namespace SST;
using namespace SST::Interfaces;

namespace SST {
namespace Astra {

AstraNIC::AstraNIC(ComponentId_t id, Params &params, int nicID) : SubComponent(id), nicID_(nicID) {
//AstraNIC::AstraNIC(ComponentId_t id, Params &params) : SubComponent(id) {
    out_ = new Output("", 1, 0, Output::STDOUT);
    dbg_ = new Output("[\@f:\@l:\@p:\@t] ", 1, 0, Output::STDERR);

    networkInterface_ = new AstraNetworkInterface(nicID_, *this);
    std::string lctype = params.find<std::string>("linkcontrol", "merlin.linkcontrol");
    Params lcparams;
	lcparams.insert("link_bw", params.find<std::string>("network_bw", "80GiB/s"));
	lcparams.insert("in_buf_size", params.find<std::string>("network_input_buffer_size", "1KiB"));
	lcparams.insert("out_buf_size", params.find<std::string>("network_output_buffer_size", "1KiB"));

    std::string freq_ = params.find<std::string>("frequency", "2.0GHz");
    registerClock(freq_, new Clock::Handler<AstraNIC, &AstraNIC::tick>(this));

    // TODO: get from params?
    std::string portName = "port" + std::to_string(nicID_);

    lcparams.insert("port_name", portName);
    dbg_->debug(CALL_INFO, 1, 0, "Loading linkController:\n");
    dbg_->debug(CALL_INFO, 1, 0, "  nicID_: %d\n", nicID);
    dbg_->debug(CALL_INFO, 1, 0, "  type: %s\n", lctype.c_str());
    dbg_->debug(CALL_INFO, 1, 0, "  portname: %s\n", portName.c_str());
    linkControl_ = loadAnonymousSubComponent<SST::Interfaces::SimpleNetwork>(lctype, portName, 0, ComponentInfo::SHARE_PORTS | ComponentInfo::INSERT_STATS, lcparams, 1);
    if (!linkControl_) {
        out_->fatal(CALL_INFO, 1, "Failed to load linkcontroller\n");
    }
};

//AstraNIC::AstraNIC(ComponentId_t id) : SubComponent(id) { }

AstraNetworkInterface* AstraNIC::getNetworkInterface() {
    return networkInterface_;
}

SimTime_t AstraNIC::getCurrentSimTimeNanoWrapper() {
    return getCurrentSimTimeNano();
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

    if (sendQueue.empty()) disableClock = true;
    return disableClock;
}

// Called by networkInterface_ to send a packet
void AstraNIC::send(AstraEvent* ae) {
    dbg_->debug(CALL_INFO, 1, 0, "nicID=%d Received send event from AstraNetworkInterface\n", nicID_);
    auto req = new SimpleNetwork::Request();
    req->src = nicID_;
    req->dest = ae->dst_;
    req->givePayload(ae);
    sendQueue.push(req);
    disableClock = false; // Re-enable the clock so we can send this event across the link
}

} // namespace Astra
} // namespace SST

