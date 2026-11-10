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

#ifndef _H_VANADIS_FUNCTIONAL_UNIT
#define _H_VANADIS_FUNCTIONAL_UNIT

#include <cinttypes>
#include <climits>
#include <cstdint>
#include <deque>

#include "inst/vinst.h"
#include "vregreadycheck.h"
#include "vpipelinetrace.h"


namespace SST {
namespace Vanadis {

class VanadisFunctionalUnitInsRecord {
public:
    VanadisFunctionalUnitInsRecord(VanadisInstruction* base_ins, uint16_t cycles)
        : ins(base_ins), cycles_left(cycles) {}
    ~VanadisFunctionalUnitInsRecord() {}

    uint16_t getCycles() const { return cycles_left; }

    bool readyToExecute() const { return (0 == cycles_left); }

    void tick() { cycles_left = (cycles_left > 0) ? cycles_left - 1 : 0; }

    uint32_t getHardwareThread() const { return ins->getHWThread(); }

    VanadisInstruction* getInstruction() { return ins; }

private:
    VanadisInstruction* ins;
    uint16_t cycles_left;
};

class VanadisFunctionalUnit {

public:
    VanadisFunctionalUnit(uint16_t id, VanadisFunctionalUnitType unit_type, uint16_t lat)
        : fu_id(id), fu_type(unit_type), latency(lat), accept_this_cycle(true) {
    }

    ~VanadisFunctionalUnit() {
        for (auto q_itr = pending_execute.begin(); q_itr != pending_execute.end(); q_itr++) {
            delete (*q_itr);
        }
    }

    VanadisFunctionalUnitType getType() const { return fu_type; }

    bool isInstructionSlotFree() const { return accept_this_cycle; }

    void insertInstruction(VanadisInstruction* ins) {
        //assert(accept_this_cycle == true);
        pending_execute.push_back(new VanadisFunctionalUnitInsRecord(ins, latency));
        accept_this_cycle = false;
    }

    uint16_t getUnitID() const { return fu_id; }

    // Gives this FU pointers to the owning core's physical-register ready-bit
    // vectors (indexed by physical register id, shared pool across hw-threads) plus the core's
    // reg_ready_at_writeback flag. Pointers stay null (default) unless the core wires them up,
    // and markRegsReadyIfEnabled() below is a no-op unless the flag is true, so this has zero
    // effect on the default-off path.
    void setRegReadyBits(std::vector<uint8_t>* int_reg_ready, std::vector<uint8_t>* fp_reg_ready, const bool* flag) {
        int_reg_ready_ = int_reg_ready;
        fp_reg_ready_ = fp_reg_ready;
        reg_ready_at_writeback_ = flag;
    }

    // Wires up the shadow checker (no-op unless its own "reg_ready_check" debug
    // param is on; see vregreadycheck.h).
    void setRegReadyChecker(VanadisRegReadyChecker* checker) { reg_ready_checker_ = checker; }

    // Wires up the debug-only stage-timestamp dumper (no-op unless its own "stage_trace" debug
    // param is on; see vpipelinetrace.h).
    void setPipelineTracer(VanadisPipelineTracer* tracer) { pipeline_tracer_ = tracer; }

    void tick(const uint64_t cycle, SST::Output* output, std::vector<VanadisRegisterFile*>& regFile) {
        int k_in=0;
        for(auto q_itr = pending_execute.begin(); q_itr != pending_execute.end();) {
            VanadisFunctionalUnitInsRecord* q_item = (*q_itr);

            if(q_item->readyToExecute())
            {
                VanadisInstruction* inner_ins = q_item->getInstruction();

                // This is the actual read point for this instruction's source
                // operands (execute() reads them to compute its result) -- check before, not
                // after, the read happens.
                if ( nullptr != reg_ready_checker_ ) { reg_ready_checker_->checkRead(output, inner_ins); }

                inner_ins->execute(output, regFile);

                if(LIKELY(inner_ins->completedExecution()))
                {
                    // The instruction's scalarExecute() has already written the
                    // register file by this point (markExecuted() was called from within
                    // execute()/scalarExecute()), so this is the writeback point for the
                    // ready-at-writeback path.
                    markRegsReadyIfEnabled(inner_ins);
                    if ( nullptr != reg_ready_checker_ ) { reg_ready_checker_->checkWrite(output, inner_ins); }
                    // stage_trace: this is the writeback point regardless of reg_ready_at_writeback.
                    if ( nullptr != pipeline_tracer_ ) { pipeline_tracer_->noteWriteback(inner_ins); }

                    // Delete the record entry for functional unit if the instruction marked itself executed
                    delete q_item;

                    // ready to execute, remove from pending queue
                    q_itr = pending_execute.erase(q_itr);
                }
                else
                {
                    q_itr++;
                }
            }
            else
            {
                q_item->tick();
                q_itr++;
            }
        }

        accept_this_cycle = true;
    }

    void clearByHWThreadID(SST::Output* output, const uint16_t hw_thr) {
        #ifdef VANADIS_BUILD_DEBUG
        output->verbose(CALL_INFO, 16, 0, "-> Function Unit, clearing by hardware thread %" PRIu32 "...\n", hw_thr);
        #endif

        for (auto q_itr = pending_execute.begin(); q_itr != pending_execute.end();) {
            // if we get a hardware thread match, remove and carry out
            if ((*q_itr)->getHardwareThread() == hw_thr) {
                delete (*q_itr);
                q_itr = pending_execute.erase(q_itr);
            } else {
                q_itr++;
            }
        }
    }

    void print(SST::Output* output) {
        uint16_t index = 0;

        for (auto q_itr = pending_execute.begin(); q_itr != pending_execute.end(); q_itr++) {
            VanadisFunctionalUnitInsRecord* q_front = pending_execute.front();
            output->verbose(CALL_INFO, 16, 0, "----> func-unit: %" PRIu16 " %" PRIu16 " entries / entry: %" PRIu16 " / %s / 0x%" PRI_ADDR " / cycles: %" PRIu16 " out of %" PRIu16 "\n",
                fu_id, (uint16_t) pending_execute.size(), index++, q_front->getInstruction()->getInstCode(),
                q_front->getInstruction()->getInstructionAddress(), q_front->getCycles(), latency);
        }
    }

private:
    // Marks the physical int/fp output registers of a just-completed instruction as
    // ready (ready_at_writeback path). No-op when the core hasn't wired up the pointers (default
    // off path, or any FU vector the core doesn't opt in).
    void markRegsReadyIfEnabled(VanadisInstruction* ins) {
        if (nullptr == reg_ready_at_writeback_ || !(*reg_ready_at_writeback_)) { return; }

        for (uint16_t i = 0; i < ins->countISAIntRegOut(); ++i) {
            (*int_reg_ready_)[ins->getPhysIntRegOut(i)] = 1;
        }
        for (uint16_t i = 0; i < ins->countISAFPRegOut(); ++i) {
            (*fp_reg_ready_)[ins->getPhysFPRegOut(i)] = 1;
        }
    }

    std::deque<VanadisFunctionalUnitInsRecord*> pending_execute;

    const uint16_t latency;
    VanadisFunctionalUnitType fu_type;
    const uint16_t fu_id;
    bool accept_this_cycle;

    // reg_ready_at_writeback state (see setRegReadyBits above).
    std::vector<uint8_t>* int_reg_ready_ = nullptr;
    std::vector<uint8_t>* fp_reg_ready_ = nullptr;
    const bool* reg_ready_at_writeback_ = nullptr;
    VanadisRegReadyChecker* reg_ready_checker_ = nullptr;
    VanadisPipelineTracer* pipeline_tracer_ = nullptr;
};

} // namespace Vanadis
} // namespace SST

#endif
