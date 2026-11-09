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

#ifndef _H_VANADIS_REG_READY_CHECK
#define _H_VANADIS_REG_READY_CHECK

// Shadow checker for the "reg_ready_at_writeback" feature, enabled by the separate debug param
// "reg_ready_check" (default false; see vanadis.h SST_ELI_DOCUMENT_PARAMS). This has nothing to
// do with the feature's actual issue logic -- it is a read-only observer that fatal()s as soon as
// it can prove a source physical register was read before its producer wrote it, or after the
// register was freed and handed to a different producer (use-after-free / stale writeback), or
// that an instruction wrote back into a physical register that had, in the meantime, been freed
// and reallocated out from under it.
//
// Mechanism: every *physical* register has a generation counter (bumped each time it is popped
// from the free stack for a new producer) and a "written" flag (cleared on that same allocation,
// set once the producer's value actually lands in the register file). Every consumer captures the
// generation of each source physical register at the moment it looks up the ISA->phys mapping
// (issue time, in assignRegistersToInstruction); every producer captures the generation of its
// own freshly allocated destination register(s) at that same point. At the instant a value is
// actually read (FU execute; LSQ address/data computation) or written (FU/LSQ writeback) we check
// the live generation against what was captured and that the producer already wrote back.
//
// Entirely no-op (not even a map lookup) unless "reg_ready_check" is on, and additive only: it
// never changes control flow, so it cannot affect simulated cycle counts either way.

#include <cinttypes>
#include <cstdint>
#include <unordered_map>
#include <vector>

#include <sst/core/output.h>

#include "inst/vinst.h"

namespace SST {
namespace Vanadis {

class VanadisRegReadyChecker {
public:
    using InsGenMap = std::unordered_map<VanadisInstruction*, std::vector<uint32_t>>;

    void configure(
        std::vector<uint32_t>* int_gen, std::vector<uint32_t>* fp_gen, std::vector<uint8_t>* int_written,
        std::vector<uint8_t>* fp_written, std::vector<uint64_t>* int_producer, std::vector<uint64_t>* fp_producer,
        InsGenMap* int_in_gen, InsGenMap* fp_in_gen, InsGenMap* int_out_gen, InsGenMap* fp_out_gen,
        const bool* enabled, const uint64_t* cycle, std::vector<uint8_t>* int_ready_dbg = nullptr,
        std::vector<uint8_t>* int_needs_retire_dbg = nullptr)
    {
        int_gen_ = int_gen;
        fp_gen_ = fp_gen;
        int_written_ = int_written;
        fp_written_ = fp_written;
        int_producer_ = int_producer;
        fp_producer_ = fp_producer;
        int_in_gen_ = int_in_gen;
        fp_in_gen_ = fp_in_gen;
        int_out_gen_ = int_out_gen;
        fp_out_gen_ = fp_out_gen;
        enabled_ = enabled;
        cycle_ = cycle;
        int_ready_dbg_ = int_ready_dbg;
        int_needs_retire_dbg_ = int_needs_retire_dbg;
    }

    // Issue-time capture: call once per source operand, right after the ISA->phys lookup.
    void captureIntIn(VanadisInstruction* ins, uint16_t phys_reg) {
        if ( !enabled() ) return;
        (*int_in_gen_)[ins].push_back((*int_gen_)[phys_reg]);
    }
    void captureFPIn(VanadisInstruction* ins, uint16_t phys_reg) {
        if ( !enabled() ) return;
        (*fp_in_gen_)[ins].push_back((*fp_gen_)[phys_reg]);
    }

    // Allocation-time capture: call once per destination operand, right after popping a fresh
    // physical register off the free stack (i.e. NOT for SYSCALL's no-rename reuse path).
    void allocateIntOut(VanadisInstruction* ins, uint16_t phys_reg) {
        if ( !enabled() ) return;
        (*int_gen_)[phys_reg]++;
        (*int_written_)[phys_reg] = 0;
        (*int_producer_)[phys_reg] = ins->getInstructionAddress();
        (*int_out_gen_)[ins].push_back((*int_gen_)[phys_reg]);
    }
    void allocateFPOut(VanadisInstruction* ins, uint16_t phys_reg) {
        if ( !enabled() ) return;
        (*fp_gen_)[phys_reg]++;
        (*fp_written_)[phys_reg] = 0;
        (*fp_producer_)[phys_reg] = ins->getInstructionAddress();
        (*fp_out_gen_)[ins].push_back((*fp_gen_)[phys_reg]);
    }

    // Call right before an instruction's source operands are actually read (FU execute; LSQ
    // computeLoadAddress/computeStoreAddress). fatal()s on the first violation found.
    void checkRead(SST::Output* out, VanadisInstruction* ins) {
        if ( !enabled() ) return;
        checkReadOne(out, ins, true);
        checkReadOne(out, ins, false);
    }

    // Call right after an instruction's destination operands are actually written into the
    // register file (FU writeback; LSQ markExecuted sites). fatal()s on the first violation found.
    void checkWrite(SST::Output* out, VanadisInstruction* ins) {
        if ( !enabled() ) return;
        checkWriteOne(out, ins, true);
        checkWriteOne(out, ins, false);
    }

    // Call when an instruction object is about to be deleted (retired or squashed) so that a
    // later, unrelated instruction allocated at the same (reused) heap address can never collide
    // with a leftover map entry from this one.
    void forget(VanadisInstruction* ins) {
        if ( int_in_gen_ ) int_in_gen_->erase(ins);
        if ( fp_in_gen_ ) fp_in_gen_->erase(ins);
        if ( int_out_gen_ ) int_out_gen_->erase(ins);
        if ( fp_out_gen_ ) fp_out_gen_->erase(ins);
    }

private:
    bool enabled() const { return (nullptr != enabled_) && *enabled_; }

    void checkReadOne(SST::Output* out, VanadisInstruction* ins, bool is_int) {
        InsGenMap* map_ptr = is_int ? int_in_gen_ : fp_in_gen_;
        auto       it      = map_ptr->find(ins);
        if ( it == map_ptr->end() ) return;

        std::vector<uint32_t>& gens     = it->second;
        const uint16_t         count    = is_int ? ins->countISAIntRegIn() : ins->countISAFPRegIn();
        std::vector<uint32_t>& live_gen = *(is_int ? int_gen_ : fp_gen_);
        std::vector<uint8_t>&  written  = *(is_int ? int_written_ : fp_written_);
        std::vector<uint64_t>& producer = *(is_int ? int_producer_ : fp_producer_);

        for ( uint16_t i = 0; i < count && i < gens.size(); ++i ) {
            const uint16_t phys_reg = is_int ? ins->getPhysIntRegIn(i) : ins->getPhysFPRegIn(i);

            if ( live_gen[phys_reg] != gens[i] ) {
                out->fatal(
                    CALL_INFO, -1,
                    "reg_ready_check: USE-AFTER-FREE: ins 0x%" PRIx64 " (hw_thr %" PRIu32 ") reading %s phys reg %"
                    PRIu16 " captured generation %" PRIu32 " at issue, but live generation is now %" PRIu32
                    " (current owner: ins 0x%" PRIx64 ") at cycle %" PRIu64 ". The physical register was freed and "
                    "reallocated to a new producer before this consumer actually read it.\n",
                    ins->getInstructionAddress(), ins->getHWThread(), is_int ? "int" : "fp", phys_reg, gens[i],
                    live_gen[phys_reg], producer[phys_reg], cycle_ ? *cycle_ : (uint64_t)0);
            }
            if ( !written[phys_reg] ) {
                out->fatal(
                    CALL_INFO, -1,
                    "reg_ready_check: READ-BEFORE-WRITE: ins 0x%" PRIx64 " (%s, hw_thr %" PRIu32 ") reading %s phys "
                    "reg %" PRIu16 " (generation %" PRIu32 ", producer ins 0x%" PRIx64 ") before that producer wrote "
                    "it back, at cycle %" PRIu64 ". dbg: int_ready=%d int_needs_retire=%d. The ready bit was set "
                    "before the value was actually in the register file.\n",
                    ins->getInstructionAddress(), ins->getInstCode(), ins->getHWThread(), is_int ? "int" : "fp",
                    phys_reg, gens[i], producer[phys_reg], cycle_ ? *cycle_ : (uint64_t)0,
                    (is_int && int_ready_dbg_) ? (int)(*int_ready_dbg_)[phys_reg] : -1,
                    (is_int && int_needs_retire_dbg_) ? (int)(*int_needs_retire_dbg_)[phys_reg] : -1);
            }
        }

        // NOTE: deliberately not erased here -- a single instruction's operands can be read at
        // more than one point in time (e.g. a store's address vs. its data register, or an FU
        // re-driving execute() across multiple cycles before completion), so the entry is only
        // ever removed, all at once, by forget() when the instruction object is actually
        // deleted (retired or squashed).
    }

    void checkWriteOne(SST::Output* out, VanadisInstruction* ins, bool is_int) {
        InsGenMap* map_ptr = is_int ? int_out_gen_ : fp_out_gen_;
        auto       it      = map_ptr->find(ins);
        if ( it == map_ptr->end() ) return;

        std::vector<uint32_t>& gens     = it->second;
        const uint16_t         count    = is_int ? ins->countISAIntRegOut() : ins->countISAFPRegOut();
        std::vector<uint32_t>& live_gen = *(is_int ? int_gen_ : fp_gen_);
        std::vector<uint8_t>&  written  = *(is_int ? int_written_ : fp_written_);

        for ( uint16_t i = 0; i < count && i < gens.size(); ++i ) {
            const uint16_t phys_reg = is_int ? ins->getPhysIntRegOut(i) : ins->getPhysFPRegOut(i);

            if ( live_gen[phys_reg] != gens[i] ) {
                out->fatal(
                    CALL_INFO, -1,
                    "reg_ready_check: STALE WRITEBACK: ins 0x%" PRIx64 " (hw_thr %" PRIu32 ") writing back to %s "
                    "phys reg %" PRIu16 " which it allocated at generation %" PRIu32 ", but the live generation is "
                    "now %" PRIu32 " -- the register was freed and reallocated to a different producer while this "
                    "(likely squashed) instruction was still in flight, at cycle %" PRIu64 ".\n",
                    ins->getInstructionAddress(), ins->getHWThread(), is_int ? "int" : "fp", phys_reg, gens[i],
                    live_gen[phys_reg], cycle_ ? *cycle_ : (uint64_t)0);
            }
            written[phys_reg] = 1;
        }

        // NOTE: deliberately not erased here either; see the comment in checkReadOne(). A
        // multi-cycle FU can call this more than once (harmless/idempotent) before the
        // instruction retires or is squashed, at which point forget() removes the entry.
    }

    std::vector<uint32_t>* int_gen_ = nullptr;
    std::vector<uint32_t>* fp_gen_ = nullptr;
    std::vector<uint8_t>*  int_written_ = nullptr;
    std::vector<uint8_t>*  fp_written_ = nullptr;
    std::vector<uint64_t>* int_producer_ = nullptr;
    std::vector<uint64_t>* fp_producer_ = nullptr;
    InsGenMap*              int_in_gen_ = nullptr;
    InsGenMap*              fp_in_gen_ = nullptr;
    InsGenMap*              int_out_gen_ = nullptr;
    InsGenMap*              fp_out_gen_ = nullptr;
    const bool*             enabled_ = nullptr;
    const uint64_t*         cycle_ = nullptr;
    std::vector<uint8_t>*   int_ready_dbg_ = nullptr;
    std::vector<uint8_t>*   int_needs_retire_dbg_ = nullptr;
};

} // namespace Vanadis
} // namespace SST

#endif
