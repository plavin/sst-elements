#include <sst/core/interfaces/simpleNetwork.h>
#include "astraNetworkInterface.h"
#include "astraEvent.h"
#include "astraNIC.h"

AstraNetworkInterface::AstraNetworkInterface(int rank, SST::Astra::AstraNIC& nic) : AstraNetworkAPI(rank), nic_(nic){};
AstraNetworkInterface::~AstraNetworkInterface() {};
int AstraNetworkInterface::sim_send(
        void* msg,
        uint64_t msg_size,
        int type,
        int dst,
        int tag,
        AstraSim::sim_request* request,
        void (*msg_handler)(void* fun_arg),
        void* fun_arg)
{
    auto ae = new SST::Astra::AstraEvent();
    ae->dst_ = dst;
    nic_.send(ae);

    return 0;
}

int AstraNetworkInterface::sim_recv(void* buffer,
        uint64_t count,
        int type,
        int src,
        int tag,
        AstraSim::sim_request* request,
        void (*msg_handler)(void* fun_arg),
        void* fun_arg)
{
  return 0;
}

/*
 * sim_schedule is used when ASTRA-sim wants to schedule an event on the
 * network backend. delta: The relative time difference between the current
 * time and the time when the event is triggered, as observed by the network
 * simulator. fun_ptr: The event handler to be triggered at the scheduled
 * time. fun_arg: Arguments to pass into fun_ptr.
 */
void AstraNetworkInterface::sim_schedule(AstraSim::timespec_t delta,
        void (*fun_ptr)(void* fun_arg),
        void* fun_arg)
{
    return;
}

AstraSim::timespec_t AstraNetworkInterface::sim_get_time() {
    AstraSim::timespec_t ts;
    ts.time_res = AstraSim::NS;
    ts.time_val = nic_.getCurrentSimTimeNanoWrapper();
    return ts;
}

double AstraNetworkInterface::get_BW_at_dimension(int dim) {
    return -1;
}

// Notifies that the workload for this rank has finished.
// Note that we have one network handler per rank.
// Therefore, when implementing this function, the network handler must
// find a way to concur that all ranks have finished their workloads.
void AstraNetworkInterface::sim_notify_finished(){
    return;
}
