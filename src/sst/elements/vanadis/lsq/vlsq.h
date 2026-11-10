// Copyright 2009-2026 NTESS. Under the terms
// of Contract DE-NA0003525 with NTESS, the U.S.
// Government retains certain rights in this software.
//
// Copyright (c) 2009-2026, NTESS
// All rights reserved.
//
// Portions are copyright of other developers:
// See the file CONTRIBUTORS.TXT in the top level directory
// of the distribution for more information.
//
// This file is part of the SST software package. For license
// information, see the LICENSE file in the top level directory of the
// distribution.

#ifndef _H_VANADIS_LSQ_BASE
#define _H_VANADIS_LSQ_BASE

#include <sst/core/output.h>
#include <sst/core/subcomponent.h>

#include "inst/regfile.h"
#include "inst/vfence.h"
#include "inst/vload.h"
#include "inst/vstore.h"
#include "../vregreadycheck.h"
#include "../vmemordercheck.h"
#include "../vpipelinetrace.h"

#include <cassert>
#include <cinttypes>
#include <cstdint>
#include <vector>
#include <queue>

#define VANADIS_DBG_LSQ_STORE_FLG 0 // (1<<0)
#define VANADIS_DBG_LSQ_LOAD_FLG  0 //(1<<1)

namespace SST {
namespace Vanadis {

class VanadisLoadStoreQueue : public SST::SubComponent {

public:
    SST_ELI_REGISTER_SUBCOMPONENT_API(SST::Vanadis::VanadisLoadStoreQueue, int, int)

    SST_ELI_DOCUMENT_PARAMS({ "verbose", "Set the verbosity of output for the LSQ", "0" },
                            { "verbose_mask", "Mask bits for masking output", "-1" },
                            { "debug_ins_addrs", "Comma-separated list of instruction addresses to debug", ""},
                            { "debug_addrs", "Comma-separated list of memory addresses to debug", ""},
                            { "verboseMask", "DEPRECATED - use 'verbose_mask' instead. Mask bits for masking output", "-1" },
                            { "dbgInsAddrs", "DEPRECATED - use 'debug_ins_addrs' instead. Comma-separated list of instruction addresses to debug", ""},
                            { "dbgAddrs", "DEPRECATED - use 'debug_addrs' instead. Comma-separated list of addresses to debug", ""},
            )

    /*
     * Constructor takes two additional parameters
     * coreid - an integer specifying the core ID for the core that loaded this LSQ
     *          - Each vanadis core should have a unique and sequential ID. E.g., for one core the ID=0, for two cores, one has ID=0 and one has ID=1
     * hwthreads - the total number of hardware threads supported by the core. The LSQ will use this to appropriately partition queues and detect
     *              ordering violations
     */
    VanadisLoadStoreQueue(ComponentId_t id, Params& params, uint32_t core_id, uint32_t hw_threads) : SubComponent(id) {

        uint32_t verbosity = params.find<uint32_t>("verbose");
        uint32_t mask = params.find<uint32_t>("verboseMask",-1);
        std::string prefix = "[lsq " + getName() + " !t]: ";
        output_ = new SST::Output(prefix, verbosity, mask, SST::Output::STDOUT);

        #ifdef VANADIS_BUILD_DEBUG
        setDbgInsAddrs( params.find<std::string>("dbgInsAddrs", "") );
        setDbgAddrs( params.find<std::string>("dbgAddrs", "") );
        #endif

        core_id_ = core_id;
        hw_threads_ = hw_threads;
        register_files_ = nullptr;
    }

    virtual ~VanadisLoadStoreQueue() { delete output_; }

    void setRegisterFiles(std::vector<VanadisRegisterFile*>* reg_f) {
        #ifdef VANADIS_BUILD_DEBUG
        output_->verbose(CALL_INFO, 8, 0, "Setting register files (%" PRIu32 " register files in set)\n",
                        (uint32_t)reg_f->size());
        #endif
        assert(reg_f != nullptr);

        register_files_ = reg_f;
    }

    // Gives the LSQ access to the owning core's physical-register ready-bit vectors (indexed
    // by physical register id) and its reg_ready_at_writeback flag, so load/store/fence
    // completion can mark output registers ready at writeback instead of only at retire.
    // Pointers stay null, and markRegReadyIfEnabled() below is a no-op, unless the core wires
    // this up, so this has no effect on the default-off path.
    void setRegReadyBits(std::vector<uint8_t>* int_reg_ready, std::vector<uint8_t>* fp_reg_ready, const bool* flag) {
        int_reg_ready_ = int_reg_ready;
        fp_reg_ready_ = fp_reg_ready;
        reg_ready_at_writeback_ = flag;
    }

    // Wires up the shadow checker (no-op unless its own "reg_ready_check" debug param is on;
    // see vregreadycheck.h).
    void setRegReadyChecker(VanadisRegReadyChecker* checker) { reg_ready_checker_ = checker; }

    // Wires up the memory-order shadow checker (no-op unless its own "mem_order_check" debug
    // param is on; see ../vmemordercheck.h).
    void setMemOrderChecker(VanadisMemOrderChecker* checker) { mem_order_checker_ = checker; }

    // Wires up the debug-only stage-timestamp dumper (no-op unless its own "stage_trace" debug
    // param is on; see ../vpipelinetrace.h).
    void setPipelineTracer(VanadisPipelineTracer* tracer) { pipeline_tracer_ = tracer; }

    void setCoreId( int core ) { core_id_ = core; }
    int getCoreId( ) { return core_id_; }

    void setHWThreads( int threads ) { hw_threads_ = threads; }
    int getHWThreads( ) { return hw_threads_; }

    virtual bool storeFull() = 0;
    virtual bool loadFull() = 0;
    virtual bool storeBufferFull() = 0;

    virtual size_t storeSize() = 0;
    virtual size_t loadSize() = 0;
    virtual size_t storeBufferSize() = 0;

    virtual void push(VanadisStoreInstruction* store_me) = 0;
    virtual void push(VanadisLoadInstruction* load_me) = 0;
    virtual void push(VanadisFenceInstruction* fence) = 0;

    virtual void tick(uint64_t cycle) = 0;
    virtual void clearLSQByThreadID(const uint32_t thread) = 0;

    virtual void init(unsigned int phase) = 0;

    virtual void printStatus(SST::Output& output) {}

protected:

    void setDbgInsAddrs( std::string addrs ) {

        while ( ! addrs.empty() ) {
            printf("%s() %s\n",__func__,addrs.c_str());
            auto pos = addrs.find(',');
            std::string addr;
            if ( pos == std::string::npos ) {
                addr = addrs;
                addrs.clear();
            } else  {
                addr = addrs.substr(0,pos);
                addrs = addrs.substr(pos+1);
            }
            dbg_ins_addrs_.push_back(  strtol( addr.c_str(), NULL , 16 ) );
        }
    }


    bool isDbgInsAddr( uint64_t addr ) {
        for ( auto& it : dbg_ins_addrs_ ) {
            if ( it == addr ) return true;
        }
        return false;
    }

    void setDbgAddrs( std::string addrs ) {

        while ( ! addrs.empty() ) {
            printf("%s() %s\n",__func__,addrs.c_str());
            auto pos = addrs.find(',');
            std::string addr;
            if ( pos == std::string::npos ) {
                addr = addrs;
                addrs.clear();
            } else  {
                addr = addrs.substr(0,pos);
                addrs = addrs.substr(pos+1);
            }
            dbg_addrs_.push_back(  strtol( addr.c_str(), NULL , 16 ) );
        }
    }


    bool isDbgAddr( uint64_t addr ) {
        for ( auto& it : dbg_addrs_ ) {
            if ( it == addr ) return true;
        }
        return false;
    }

    // Marks the physical int/fp output registers of a just-completed load/store/fence as
    // ready (ready_at_writeback path). No-op when the core hasn't wired up the pointers.
    void markRegReadyIfEnabled(VanadisInstruction* ins) {
        if (nullptr == reg_ready_at_writeback_ || !(*reg_ready_at_writeback_)) { return; }

        for (uint16_t i = 0; i < ins->countISAIntRegOut(); ++i) {
            (*int_reg_ready_)[ins->getPhysIntRegOut(i)] = 1;
        }
        for (uint16_t i = 0; i < ins->countISAFPRegOut(); ++i) {
            (*fp_reg_ready_)[ins->getPhysFPRegOut(i)] = 1;
        }
    }

    // Call right before actually reading an instruction's source operands (address and/or
    // data register(s)) out of the register file. No-op unless "reg_ready_check" is on.
    void checkRegReadyReadIfEnabled(VanadisInstruction* ins) {
        if ( nullptr != reg_ready_checker_ ) { reg_ready_checker_->checkRead(output_, ins); }
    }

    // Call right after an instruction's destination operand(s) have actually been written
    // into the register file (alongside markRegReadyIfEnabled above). No-op unless
    // "reg_ready_check" is on.
    void checkRegReadyWriteIfEnabled(VanadisInstruction* ins) {
        if ( nullptr != reg_ready_checker_ ) { reg_ready_checker_->checkWrite(output_, ins); }
    }

    // mem_order_check capture hooks (no-ops unless "mem_order_check" is on; see
    // ../vmemordercheck.h). Callers must only call these for ordinary (MEM_TRANSACTION_NONE)
    // loads/stores -- LL/SC/AMO/LOCK are deliberately never captured.
    void captureStoreChunkIfEnabled(VanadisInstruction* store_ins, uint64_t address, const uint8_t* data, uint64_t width) {
        if ( nullptr != mem_order_checker_ ) { mem_order_checker_->captureStoreChunk(store_ins, address, data, width); }
    }
    void captureLoadChunkIfEnabled(VanadisInstruction* load_ins, uint64_t address, const uint8_t* data, uint64_t width) {
        if ( nullptr != mem_order_checker_ ) { mem_order_checker_->captureLoadChunk(load_ins, address, data, width); }
    }
    void noteStoreAckIfEnabled(uint64_t address, uint64_t width) {
        if ( nullptr != mem_order_checker_ ) { mem_order_checker_->noteStoreAck(address, width); }
    }

    // Call the instant an LL/SC/LOCK store's write is dispatched to memory (these transaction
    // types are deliberately never captured by captureStoreChunkIfEnabled -- see
    // vmemordercheck.h) to conservatively drop the shadow's opinion of the bytes it's about to
    // overwrite, avoiding a false positive against a stale pre-AMO shadow value. No-op unless
    // mem_order_check is on.
    void invalidateMemOrderRangeIfEnabled(uint64_t address, uint64_t width) {
        if ( nullptr != mem_order_checker_ ) { mem_order_checker_->invalidateRange(address, width); }
    }

    // stage_trace hooks (no-ops unless "stage_trace" is on; see ../vpipelinetrace.h). Call
    // noteLsqSendIfEnabled the instant a load/store request is actually dispatched to the memory
    // system, and noteMemRespIfEnabled when it completes there (for an ordinary store this is the
    // same cycle as the send, since issueStoreFront() is fire-and-forget).
    void noteLsqSendIfEnabled(VanadisInstruction* ins) {
        if ( nullptr != pipeline_tracer_ ) { pipeline_tracer_->noteLsqSend(ins); }
    }
    void noteMemRespIfEnabled(VanadisInstruction* ins) {
        if ( nullptr != pipeline_tracer_ ) { pipeline_tracer_->noteMemResp(ins); }
    }
    // Call alongside markRegReadyIfEnabled above -- this is the LSQ-side writeback point.
    void noteWritebackIfEnabled(VanadisInstruction* ins) {
        if ( nullptr != pipeline_tracer_ ) { pipeline_tracer_->noteWriteback(ins); }
    }

    std::deque<uint64_t> dbg_ins_addrs_;
    std::deque<uint64_t> dbg_addrs_;
    uint32_t core_id_;
    uint32_t hw_threads_;
    std::vector<VanadisRegisterFile*>* register_files_;
    SST::Output* output_;

    // reg_ready_at_writeback state (see setRegReadyBits above).
    std::vector<uint8_t>* int_reg_ready_ = nullptr;
    std::vector<uint8_t>* fp_reg_ready_ = nullptr;
    const bool* reg_ready_at_writeback_ = nullptr;
    VanadisRegReadyChecker* reg_ready_checker_ = nullptr;
    VanadisMemOrderChecker* mem_order_checker_ = nullptr;
    VanadisPipelineTracer* pipeline_tracer_ = nullptr;
};

} // namespace Vanadis
} // namespace SST

#endif
