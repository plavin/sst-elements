#pragma once

#include <sst/core/sst_types.h>
#include <sst/core/event.h>

using namespace std;

namespace SST {
namespace Astra {

class AstraEvent : public SST::Event {
private:
public:
    AstraEvent() : SST::Event() {}
    void* buffer_; //TODO
    uint64_t count_;
    int type_;
    int src_;
    int dst_;
    int tag_;
    AstraSim::sim_request* request_; //TODO
    void (*msg_handler_)(void* fun_arg); //TODO
    void* fun_arg_; //TODO

    void serialize_order(SST::Core::Serialization::serializer &ser)  override {
        SST::Event::serialize_order(ser);
        //SST_SER(buffer_); //TODO
        SST_SER(count_);
        SST_SER(type_);
        SST_SER(src_);
        SST_SER(dst_);
        SST_SER(tag_);
        //SST_SER(request_); //TODO
        //SST_SER(*msg_handler); //TODO
        //SST_SER(fun_arg_); //TODO

    }

    ImplementSerializable(SST::Astra::AstraEvent);

}; // class AstraEvent

} // namespace Astra
} // namespace SST

