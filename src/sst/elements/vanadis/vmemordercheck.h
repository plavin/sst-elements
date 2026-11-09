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

#ifndef _H_VANADIS_MEM_ORDER_CHECK
#define _H_VANADIS_MEM_ORDER_CHECK

// Byte-granular shadow-memory checker, enabled by the debug param "mem_order_check" (default
// false; see vanadis.h SST_ELI_DOCUMENT_PARAMS). Read-only observer: it never changes
// issue/execute/retire control flow, so leaving it off (default) or on cannot change simulated
// cycle counts.
//
// Mechanism: maintain a byte-granular shadow of the values this core's own ordinary
// (MEM_TRANSACTION_NONE -- i.e. not LL/SC/AMO/LOCK, which are deliberately skipped) stores have
// written, keyed by virtual address. A store's payload bytes are captured at the
// instant they are read out of the register file to build the memory write request
// (VanadisBasicLoadStoreQueue::issueStore(), lsq/vbasiclsq.h) and are committed into the shadow
// at the instant the store actually *retires* (VanadisCore::performRetire, vanadis.cc) --
// retirement is the one point program order is enforced regardless of relative memory-system
// timing, so committing there makes the shadow a correct, ordered model of architectural memory
// state. A load's returned bytes are captured per response chunk as they arrive from memory
// (StandardMemHandlers::handle(ReadResp*), lsq/vbasiclsq.h) and are checked against the shadow at
// the instant the load retires. Any byte the shadow has never seen (OS-provided/initial memory,
// or memory never written by an ordinary store from this core) is skipped, never flagged.
//
// The shadow is cleared in full at every SYSCALL retire (the OS can read/write memory this core's
// own store stream never observed, e.g. mmap/brk'd pages, pipe/file contents, etc; the conservative
// whole-shadow clear trades a small loss of coverage right after a syscall for zero false
// positives).

#include <cinttypes>
#include <cstdint>
#include <unordered_map>
#include <vector>

#include <sst/core/output.h>

#include "inst/vinst.h"

namespace SST {
namespace Vanadis {

class VanadisMemOrderChecker {
public:
    void configure(const bool* enabled, const uint64_t* cycle) {
        enabled_ = enabled;
        cycle_   = cycle;
    }

    bool enabled() const { return (nullptr != enabled_) && *enabled_; }

    // Call from the LSQ at the point a store's payload bytes are actually read out of the
    // register file and handed to the memory interface (one call per contiguous chunk, so split
    // stores just call this twice). Callers must gate out LL/SC/AMO/LOCK transactions themselves
    // (ordinary MEM_TRANSACTION_NONE stores only); this is a no-op if never called for a given
    // store, which is exactly what we want for those.
    void captureStoreChunk(VanadisInstruction* store_ins, uint64_t address, const uint8_t* data, uint64_t width) {
        if ( !enabled() ) return;
        Chunk c;
        c.address = address;
        c.data.assign(data, data + width);
        c.cycle = cycle_ ? *cycle_ : (uint64_t)0;
        store_capture_[store_ins].push_back(std::move(c));
    }

    // Call from the LSQ's read-response handler, once per response chunk (split loads get two
    // calls), for ordinary MEM_TRANSACTION_NONE loads only -- same gating convention as above.
    void captureLoadChunk(VanadisInstruction* load_ins, uint64_t address, const uint8_t* data, uint64_t width) {
        if ( !enabled() ) return;
        Chunk c;
        c.address = address;
        c.data.assign(data, data + width);
        c.cycle = cycle_ ? *cycle_ : (uint64_t)0;
        load_capture_[load_ins].push_back(std::move(c));
    }

    // Call from the LSQ's write-response handler (any store, standard or otherwise) so mismatch
    // diagnostics can report when the underlying write was actually acknowledged by the memory
    // system. Purely diagnostic / best-effort: a later store to the same bytes simply overwrites
    // this, and it is never consulted to decide whether a violation occurred.
    void noteStoreAck(uint64_t address, uint64_t width) {
        if ( !enabled() ) return;
        const uint64_t now = cycle_ ? *cycle_ : (uint64_t)0;
        for ( uint64_t i = 0; i < width; ++i ) {
            store_ack_cycle_[address + i] = now;
        }
    }

    // Call at store retire (VanadisCore::performRetire): commits any captured payload chunks into
    // the shadow, in program order. No-op if this instruction never captured anything (not a
    // store, trapped before issuing, or an LL/SC/AMO/LOCK store that was deliberately not
    // captured).
    void commitStore(VanadisInstruction* store_ins) {
        if ( !enabled() ) return;
        auto it = store_capture_.find(store_ins);
        if ( it == store_capture_.end() ) return;

        const uint64_t now = cycle_ ? *cycle_ : (uint64_t)0;
        for ( const Chunk& chunk : it->second ) {
            for ( size_t i = 0; i < chunk.data.size(); ++i ) {
                ShadowByte& sb      = shadow_[chunk.address + i];
                sb.value            = chunk.data[i];
                sb.store_pc         = store_ins->getInstructionAddress();
                sb.store_issue_cycle   = chunk.cycle;
                sb.store_commit_cycle  = now;
            }
        }
        store_capture_.erase(it);
    }

    // Call at load retire (VanadisCore::performRetire): checks captured response bytes against
    // the shadow for every byte the shadow has an opinion on. fatal()s on the first mismatch
    // found. No-op if this instruction never captured anything (not a load, trapped, or an
    // LL/SC/AMO/LOCK load that was deliberately not captured).
    void checkLoad(SST::Output* out, VanadisInstruction* load_ins) {
        if ( !enabled() ) return;
        auto it = load_capture_.find(load_ins);
        if ( it == load_capture_.end() ) return;

        const uint64_t now = cycle_ ? *cycle_ : (uint64_t)0;
        for ( const Chunk& chunk : it->second ) {
            for ( size_t i = 0; i < chunk.data.size(); ++i ) {
                const uint64_t addr = chunk.address + i;
                auto            sit = shadow_.find(addr);
                if ( sit == shadow_.end() ) continue; // never written by this core's store stream

                const ShadowByte& sb = sit->second;
                if ( sb.value != chunk.data[i] ) {
                    auto ack_it = store_ack_cycle_.find(addr);
                    const int64_t ack_cycle = (ack_it != store_ack_cycle_.end()) ? (int64_t)ack_it->second : -1;

                    out->fatal(
                        CALL_INFO, -1,
                        "mem_order_check: MEMORY-ORDER VIOLATION: load ins 0x%" PRIx64 " (%s, hw_thr %" PRIu32
                        ") retiring at cycle %" PRIu64 " read byte at addr 0x%" PRIx64 " = 0x%02x (read from "
                        "memory at cycle %" PRIu64 "), but the shadow (last store to retire to this byte) says "
                        "it should be 0x%02x -- store pc 0x%" PRIx64 ", issued-to-memory at cycle %" PRIu64 ", "
                        "write acked (any store) at cycle %" PRId64 ", committed/retired at cycle %" PRIu64
                        ". This is a real memory-ordering bug: the load observed a value from memory that is "
                        "stale relative to program order.\n",
                        load_ins->getInstructionAddress(), load_ins->getInstCode(), load_ins->getHWThread(), now,
                        addr, chunk.data[i], chunk.cycle, sb.value, sb.store_pc, sb.store_issue_cycle, ack_cycle,
                        sb.store_commit_cycle);
                }
            }
        }
        load_capture_.erase(it);
    }

    // Clear the whole shadow (e.g. at every SYSCALL retire -- the OS can write memory this core's
    // own store stream never observed).
    void clearShadow() {
        if ( !enabled() ) return;
        shadow_.clear();
    }

    // LL/SC/LOCK/AMO stores are deliberately never *captured* (see commitStore()'s doc comment),
    // but an AMO (built from LL+arith+SC micro-ops) still legitimately writes a new value to a
    // byte; without this, a later ordinary load to that byte would be checked against the
    // shadow's stale pre-AMO opinion and produce a false-positive violation. Call this from the
    // LSQ the instant an LL/SC/LOCK store's write is dispatched to memory (same point ordinary
    // stores call captureStoreChunk) to conservatively invalidate (erase, not overwrite with a
    // guessed value) the shadow's opinion of the bytes it touches -- mirroring clearShadow()'s
    // "we don't know, so stay silent rather than risk a false positive" policy, just scoped to
    // this store's own byte range instead of the whole address space.
    void invalidateRange(uint64_t address, uint64_t width) {
        if ( !enabled() ) return;
        for ( uint64_t i = 0; i < width; ++i ) {
            shadow_.erase(address + i);
        }
    }

    // Call when an instruction object is about to be deleted (retired or squashed) so a later,
    // unrelated instruction allocated at the same (reused) heap address can never collide with a
    // stale capture entry.
    void forget(VanadisInstruction* ins) {
        store_capture_.erase(ins);
        load_capture_.erase(ins);
    }

private:
    struct Chunk {
        uint64_t             address;
        std::vector<uint8_t> data;
        uint64_t             cycle;
    };

    struct ShadowByte {
        uint8_t  value              = 0;
        uint64_t store_pc           = 0;
        uint64_t store_issue_cycle  = 0;
        uint64_t store_commit_cycle = 0;
    };

    std::unordered_map<VanadisInstruction*, std::vector<Chunk>> store_capture_;
    std::unordered_map<VanadisInstruction*, std::vector<Chunk>> load_capture_;
    std::unordered_map<uint64_t, ShadowByte>                    shadow_;
    std::unordered_map<uint64_t, uint64_t>                      store_ack_cycle_;

    const bool*     enabled_ = nullptr;
    const uint64_t* cycle_   = nullptr;
};

} // namespace Vanadis
} // namespace SST

#endif
