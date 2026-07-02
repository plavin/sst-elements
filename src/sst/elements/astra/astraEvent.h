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
    int tag_;
    void (*msg_handler_)(void* fun_arg);
    void* fun_arg_;
    bool tail_;

    void serialize_order(SST::Core::Serialization::serializer &ser)  override {
        SST::Event::serialize_order(ser);
        SST_SER(tag_);
        SST_SER((uintptr_t)msg_handler_); //TODO - will this work?
        SST_SER((uintptr_t)fun_arg_);
        SST_SER(tail_);
    }

    ImplementSerializable(SST::Astra::AstraEvent);

}; // class AstraEvent

} // namespace Astra
} // namespace SST

