#ifndef ASTRANETWORKINTERFACE_H
#define ASTRANETWORKINTERFACE_H

#include "astra-sim/common/AstraNetworkAPI.hh"

namespace SST {
  namespace Astra {
    class AstraNIC;
  }
}

class AstraNetworkInterface : public AstraSim::AstraNetworkAPI {
  public:
    AstraNetworkInterface(int, SST::Astra::AstraNIC& nic);
    ~AstraNetworkInterface();

    int sim_send(void* buffer,
                         uint64_t count,
                         int type,
                         int dst,
                         int tag,
                         AstraSim::sim_request* request,
                         void (*msg_handler)(void* fun_arg),
                         void* fun_arg) override;

    int sim_recv(void* buffer,
                         uint64_t count,
                         int type,
                         int src,
                         int tag,
                         AstraSim::sim_request* request,
                         void (*msg_handler)(void* fun_arg),
                         void* fun_arg) override;

    /*
     * sim_schedule is used when ASTRA-sim wants to schedule an event on the
     * network backend. delta: The relative time difference between the current
     * time and the time when the event is triggered, as observed by the network
     * simulator. fun_ptr: The event handler to be triggered at the scheduled
     * time. fun_arg: Arguments to pass into fun_ptr.
     */
    void sim_schedule(AstraSim::timespec_t delta,
                              void (*fun_ptr)(void* fun_arg),
                              void* fun_arg) override;

    AstraSim::timespec_t sim_get_time() override;

    double get_BW_at_dimension(int dim) override;

    // Notifies that the workload for this rank has finished.
    // Note that we have one network handler per rank.
    // Therefore, when implementing this function, the network handler must
    // find a way to concur that all ranks have finished their workloads.
    void sim_notify_finished() override;

private:
    int rank_;
    SST::Astra::AstraNIC& nic_;
};

#endif
