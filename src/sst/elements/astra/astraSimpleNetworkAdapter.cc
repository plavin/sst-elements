#include "astraSimpleNetworkAdapter.h"

AstraSimpleNetworkAdapter::AstraSimpleNetworkAdapter(int rank, SST::Astra::AstraConnector& owner) : AstraNetworkAPI(rank), owner_(owner){};
AstraSimpleNetworkAdapter::~AstraSimpleNetworkAdapter() {};
int AstraSimpleNetworkAdapter::sim_send(void* buffer,
        uint64_t count,
        int type,
        int dst,
        int tag,
        AstraSim::sim_request* request,
        void (*msg_handler)(void* fun_arg),
        void* fun_arg)
{
  return 0;
}

int AstraSimpleNetworkAdapter::sim_recv(void* buffer,
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
void AstraSimpleNetworkAdapter::sim_schedule(AstraSim::timespec_t delta,
        void (*fun_ptr)(void* fun_arg),
        void* fun_arg)
{
    return;
}

AstraSim::timespec_t AstraSimpleNetworkAdapter::sim_get_time() {
    AstraSim::timespec_t ts;
    ts.time_res = AstraSim::NS;
    ts.time_val = owner_.getCurrentSimTimeNanoWrapper();
    return ts;
}

double AstraSimpleNetworkAdapter::get_BW_at_dimension(int dim) {
    return -1;
}

// Notifies that the workload for this rank has finished. 
// Note that we have one network handler per rank. 
// Therefore, when implementing this function, the network handler must 
// find a way to concur that all ranks have finished their workloads.
void AstraSimpleNetworkAdapter::sim_notify_finished(){
    return;
}
