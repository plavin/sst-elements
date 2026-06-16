#include <sst/core/sst_config.h>
#include "sst/elements/astra/astraNIC.h"

//using namespace SST;
//using namespace SST::Astra;

namespace SST {
namespace Astra {

AstraNIC::AstraNIC(ComponentId_t id, Params &params, int rank_) : SubComponent(id) {
    networkInterface_ = new AstraNetworkInterface(rank_);
    std::string lctype = params.find<std::string>("linkcontrol", "merlin.linkcontrol");
    Params lcparams;
	lcparams.insert("link_bw", params.find<std::string>("network_bw", "80GiB/s"));
	lcparams.insert("in_buf_size", params.find<std::string>("network_input_buffer_size", "1KiB"));
	lcparams.insert("out_buf_size", params.find<std::string>("network_output_buffer_size", "1KiB"));
	lcparams.insert("port_name", params.find<std::string>("port", "")); //TODO - port name??

    linkControl_ = loadAnonymousSubComponent<SST::Interfaces::SimpleNetwork>(lctype, "port", 0, ComponentInfo::SHARE_PORTS | ComponentInfo::INSERT_STATS, lcparams, 1); // TODO "port" param??
};

AstraNetworkInterface* AstraNIC::getNetworkInterface() {
    return networkInterface_;
}

} // namespace Astra
} // namespace SST

