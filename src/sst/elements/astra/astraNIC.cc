#include <sst/core/sst_config.h>
#include "sst/elements/astra/astraNIC.h"

//using namespace SST;
using namespace SST::Interfaces;

namespace SST {
namespace Astra {

AstraNIC::AstraNIC(ComponentId_t id, Params &params, int nicID) : SubComponent(id), nicID_(nicID) {
//AstraNIC::AstraNIC(ComponentId_t id, Params &params) : SubComponent(id) {
    networkInterface_ = new AstraNetworkInterface(nicID_, *this);
    std::string lctype = params.find<std::string>("linkcontrol", "merlin.linkcontrol");
    Params lcparams;
	lcparams.insert("link_bw", params.find<std::string>("network_bw", "80GiB/s"));
	lcparams.insert("in_buf_size", params.find<std::string>("network_input_buffer_size", "1KiB"));
	lcparams.insert("out_buf_size", params.find<std::string>("network_output_buffer_size", "1KiB"));
	lcparams.insert("port_name", params.find<std::string>("port", "")); //TODO - port name??

    /*
    freq_ = params.find<std::string>("frequency", "2.0GHz");
    registerClock(freq_, new Clock::Handler<AstraNIC, &AstraNIC::tick>(this));
    */


    //TODO - reenable
    // linkControl_ = loadAnonymousSubComponent<SST::Interfaces::SimpleNetwork>(lctype, "port", 0, ComponentInfo::SHARE_PORTS | ComponentInfo::INSERT_STATS, lcparams, 1); // TODO "port" param??
};

//AstraNIC::AstraNIC(ComponentId_t id) : SubComponent(id) { }

AstraNetworkInterface* AstraNIC::getNetworkInterface() {
    return networkInterface_;
}

SimTime_t AstraNIC::getCurrentSimTimeNanoWrapper() {
    return getCurrentSimTimeNano();
}

// Called by networkInterface_ to send a packet
void AstraNIC::send(AstraEvent* ae) {
    auto req = new SimpleNetwork::Request();
    req->src = nicID_;
    //req->dest = ae-
    req->givePayload(ae);
}


} // namespace Astra
} // namespace SST

