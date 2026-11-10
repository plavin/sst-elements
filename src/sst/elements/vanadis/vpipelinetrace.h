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

#ifndef _H_VANADIS_PIPELINE_TRACE
#define _H_VANADIS_PIPELINE_TRACE

// Debug-only, read-only per-instruction stage-timestamp dumper. Enabled by the "stage_trace"
// param (default off; see vanadis.h), bounded by "stage_trace_skip" (retiring instructions to
// skip before dumping) and "stage_trace_count" (how many to dump after the skip; 0, the default,
// means dump nothing). Distinct from the pre-existing "pipeline_trace_file" retire-address log.
//
// Tracks, per instruction: first seen by the issue-stage scan ("seen"), allocated by
// checkInstructionResources ("issue"), dispatched to memory by the LSQ ("lsq_send", loads/stores
// only), completed in memory ("mem_resp", loads/stores only), written back ("writeback"), and
// retired ("retire", where the line is printed and the record erased). A no-op unless
// "stage_trace" is on, and purely additive bookkeeping, so it cannot affect simulated cycle
// counts.

#include <cinttypes>
#include <cstdint>
#include <cstdio>
#include <unordered_map>

#include "inst/vinst.h"

namespace SST {
namespace Vanadis {

class VanadisPipelineTracer {
public:
    void configure(const bool* enabled, const uint64_t* cycle, uint64_t skip, uint64_t count) {
        enabled_ = enabled;
        cycle_ = cycle;
        skip_ = skip;
        count_ = count;
    }

    bool enabled() const { return nullptr != enabled_ && *enabled_; }

    // First time this instruction is examined by the issue-stage scan (performIssue), whether or
    // not it ends up issuing this cycle.
    void noteSeen(VanadisInstruction* ins) {
        if ( !enabled() ) return;
        Rec& r = recs_[ins];
        if ( !r.has_seen ) { r.cycle_seen = *cycle_; r.has_seen = true; }
    }

    // checkInstructionResources passed and the instruction was allocated (assignRegistersToInstruction).
    void noteIssue(VanadisInstruction* ins) {
        if ( !enabled() ) return;
        Rec& r = recs_[ins];
        if ( !r.has_issue ) { r.cycle_issue = *cycle_; r.has_issue = true; }
    }

    // Loads/stores only: the instant the LSQ actually dispatches the request to the memory system.
    void noteLsqSend(VanadisInstruction* ins) {
        if ( !enabled() ) return;
        auto it = recs_.find(ins);
        if ( it == recs_.end() ) return;
        if ( !it->second.has_lsq_send ) { it->second.cycle_lsq_send = *cycle_; it->second.has_lsq_send = true; }
    }

    // Loads/stores only: the instant the memory system's response is processed (for an ordinary
    // store this is the same cycle as noteLsqSend -- see header comment above).
    void noteMemResp(VanadisInstruction* ins) {
        if ( !enabled() ) return;
        auto it = recs_.find(ins);
        if ( it == recs_.end() ) return;
        if ( !it->second.has_mem_resp ) { it->second.cycle_mem_resp = *cycle_; it->second.has_mem_resp = true; }
    }

    // The FU/LSQ writeback hook -- when the register file actually holds the result and (if
    // reg_ready_at_writeback is on) the ready bit would be set.
    void noteWriteback(VanadisInstruction* ins) {
        if ( !enabled() ) return;
        auto it = recs_.find(ins);
        if ( it == recs_.end() ) return;
        if ( !it->second.has_writeback ) { it->second.cycle_writeback = *cycle_; it->second.has_writeback = true; }
    }

    // Retire: print (if within the skip/count window) and always erase.
    void noteRetireAndMaybePrint(VanadisInstruction* ins) {
        if ( !enabled() ) return;
        auto it = recs_.find(ins);
        if ( it != recs_.end() ) {
            if ( retire_count_ >= skip_ && printed_ < count_ ) {
                char buf[256];
                ins->printToBuffer(buf, sizeof(buf));
                const Rec& r = it->second;
                fprintf(stdout,
                    "STAGETRACE,0x%" PRI_ADDR ",%s,seen=%" PRIu64 ",issue=%" PRIu64 ",lsq_send=%" PRIu64
                    ",mem_resp=%" PRIu64 ",writeback=%" PRIu64 ",retire=%" PRIu64 "\n",
                    ins->getInstructionAddress(), buf, r.cycle_seen, r.cycle_issue, r.cycle_lsq_send,
                    r.cycle_mem_resp, r.cycle_writeback, *cycle_);
                printed_++;
            }
            recs_.erase(it);
        }
        retire_count_++;
    }

    // Squash path: just drop the record, nothing to print (the instruction never retired).
    void forget(VanadisInstruction* ins) {
        if ( !enabled() ) return;
        recs_.erase(ins);
    }

private:
    struct Rec {
        uint64_t cycle_seen = 0, cycle_issue = 0, cycle_lsq_send = 0, cycle_mem_resp = 0, cycle_writeback = 0;
        bool has_seen = false, has_issue = false, has_lsq_send = false, has_mem_resp = false, has_writeback = false;
    };

    const bool* enabled_ = nullptr;
    const uint64_t* cycle_ = nullptr;
    uint64_t skip_ = 0;
    uint64_t count_ = 0;
    uint64_t retire_count_ = 0;
    uint64_t printed_ = 0;
    std::unordered_map<VanadisInstruction*, Rec> recs_;
};

} // namespace Vanadis
} // namespace SST

#endif
