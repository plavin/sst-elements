#include <sst/core/sst_config.h>
#include "sst/elements/astra/astraNIC.h"

using namespace SST;
using namespace SST::Astra;
using namespace SST::Interfaces;

AstraNIC::AstraNIC(ComponentID_t id, Params &params, TimeConverter tc) : SST::Interfaces::SimpleNetwork(id) {
};

void AstraNIC::send(AstraEvent* ev) {
};
AstraEvent* AstraNIC::recv() {
};

bool AstraNIC::recvNotify(int) {
};

bool AstraNIC::clock(SimTime_t cycle) {
};

size_t AstraNIC::getSizeInBits(AstraEvent *ev) {
};

void AstraNIC::init(unsigned int phase) { };
void AstraNIC::complete(unsigned int phase) { };

