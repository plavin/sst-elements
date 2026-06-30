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
    return nic_.sim_send(msg, msg_size, type, dst, tag, request, msg_handler, fun_arg);
}

int AstraNetworkInterface::sim_recv(void* msg,
        uint64_t msg_size,
        int type,
        int src,
        int tag,
        AstraSim::sim_request* request,
        void (*msg_handler)(void* fun_arg),
        void* fun_arg)
{
    return nic_.sim_recv(msg, msg_size, type, src, tag, request, msg_handler, fun_arg);
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
    nic_.sim_schedule(delta, fun_ptr, fun_arg);
    return;
}

AstraSim::timespec_t AstraNetworkInterface::sim_get_time()
{
    return nic_.sim_get_time();
}

double AstraNetworkInterface::get_BW_at_dimension(int dim) {
    return -1;
}

// Notifies that the workload for this rank has finished.
// Note that we have one network handler per rank.
// Therefore, when implementing this function, the network handler must
// find a way to concur that all ranks have finished their workloads.
void AstraNetworkInterface::sim_notify_finished(){
    nic_.sim_notify_finished();
    return;
}
