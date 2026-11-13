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

    // Wires up the core's rename_in_order flag. No-op (pointer stays null, treated as "off") on
    // any build/config that never calls this; used by the wbready-inflight-backlog backpressure
    // gate in vbasiclsq.h::issueStoreFront(), which is only safe/needed once rename_in_order lets
    // the core generate stores fast enough to build a large backlog in the first place -- see
    // that gate's own comment for the full rationale. Deliberately a separate flag from
    // reg_ready_at_writeback_ (above) so that gate stays inert for reg_ready_at_writeback-alone
    // configs (confirmed bit-identical: lsq-study/crash16m/NOTES.md).
    void setRenameInOrderFlag(const bool* flag) { rename_in_order_ = flag; }

    // Wires up the shadow checker (no-op unless its own "reg_ready_check" debug param is on;
    // see vregreadycheck.h).
    void setRegReadyChecker(VanadisRegReadyChecker* checker) { reg_ready_checker_ = checker; }

    // Wires up everything isStoreDataReadyIfLate() (below) needs to decide whether a
    // store_data_late-issued store's data register has actually been written back yet. No-op
    // (isStoreDataReadyIfLate always returns true) unless store_data_late_flag's pointee is
    // true. Requires rename_in_order (and therefore reg_ready_at_writeback / setRegReadyBits)
    // whenever store_data_late is itself on -- enforced by a fatal check in VanadisCore's
    // constructor -- so the needs_retire/retired vectors are guaranteed valid/sized then.
    void setStoreDataLateBits(
        const bool* store_data_late_flag, std::vector<uint8_t>* int_reg_needs_retire,
        std::vector<uint8_t>* int_reg_retired, std::vector<uint8_t>* fp_reg_needs_retire,
        std::vector<uint8_t>* fp_reg_retired)
    {
        store_data_late_ = store_data_late_flag;
        store_data_late_int_needs_retire_ = int_reg_needs_retire;
        store_data_late_int_retired_ = int_reg_retired;
        store_data_late_fp_needs_retire_ = fp_reg_needs_retire;
        store_data_late_fp_retired_ = fp_reg_retired;
    }

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

    // Scoped variants of the above, for a store's two inputs, which (under store_data_late) are
    // actually read from the register file at two different points in time: the address in
    // store_process() (computeStoreAddress), the data in issueStoreFront()/issueStore(). Applied
    // unconditionally (not gated on store_data_late) -- strictly more precise than checking both
    // together at the address's read point, and a no-op like the above unless "reg_ready_check"
    // is on, so it cannot change simulated cycle counts either way.
    void checkRegReadyReadAddressIfEnabled(VanadisInstruction* ins) {
        if ( nullptr != reg_ready_checker_ ) { reg_ready_checker_->checkStoreAddressRead(output_, ins); }
    }
    void checkRegReadyReadDataIfEnabled(VanadisStoreInstruction* store_ins) {
        if ( nullptr != reg_ready_checker_ ) { reg_ready_checker_->checkStoreDataRead(output_, store_ins); }
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

    // store_data_late (default off; see "store_data_late" in vanadis.h for the full design).
    // No-op (always returns true, i.e. "go ahead") unless store_data_late_ points at a true
    // flag and store_ins is an eligible store (ordinary/partial, not LL/SC/LOCK/AMO). Mirrors
    // VanadisCore::checkInstructionResources' already_renamed input-check exactly: look up the
    // data input's physical register readiness, falling back to the per-phys-reg retired_ bit
    // if needs_retire_ is set for it.
    bool isStoreDataReadyIfLate(VanadisStoreInstruction* store_ins) {
        if ( (nullptr == store_data_late_) || !(*store_data_late_) ) { return true; }
        if ( store_ins->getTransactionType() != MEM_TRANSACTION_NONE ) { return true; }

        const bool is_fp = (store_ins->getValueRegisterType() == STORE_FP_REGISTER);
        const uint16_t phys_reg = is_fp ? store_ins->getPhysFPRegIn(0) : store_ins->getPhysIntRegIn(1);

        std::vector<uint8_t>* needs_retire = is_fp ? store_data_late_fp_needs_retire_ : store_data_late_int_needs_retire_;
        std::vector<uint8_t>* retired      = is_fp ? store_data_late_fp_retired_      : store_data_late_int_retired_;
        std::vector<uint8_t>* ready        = is_fp ? fp_reg_ready_                   : int_reg_ready_;

        if ( (nullptr != needs_retire) && (phys_reg < needs_retire->size()) && (*needs_retire)[phys_reg] ) {
            return (nullptr != retired) && (phys_reg < retired->size()) && (*retired)[phys_reg];
        }
        return (nullptr != ready) && (phys_reg < ready->size()) && (*ready)[phys_reg];
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
    const bool* rename_in_order_ = nullptr;
    VanadisRegReadyChecker* reg_ready_checker_ = nullptr;

    // store_data_late state (see setStoreDataLateBits/isStoreDataReadyIfLate above).
    const bool* store_data_late_ = nullptr;
    std::vector<uint8_t>* store_data_late_int_needs_retire_ = nullptr;
    std::vector<uint8_t>* store_data_late_int_retired_ = nullptr;
    std::vector<uint8_t>* store_data_late_fp_needs_retire_ = nullptr;
    std::vector<uint8_t>* store_data_late_fp_retired_ = nullptr;
    VanadisMemOrderChecker* mem_order_checker_ = nullptr;
    VanadisPipelineTracer* pipeline_tracer_ = nullptr;
};

} // namespace Vanadis
} // namespace SST

#endif
