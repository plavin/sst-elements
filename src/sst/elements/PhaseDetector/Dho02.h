// Copyright 2009-2020 NTESS. Under the terms
// of Contract DE-NA0003525 with NTESS, the U.S.
// Government retains certain rights in this software.
//
// Copyright (c) 2009-2020, NTESS
// All rights reserved.
//
// Portions are copyright of other developers:
// See the file CONTRIBUTORS.TXT in the top level directory
// the distribution for more information.
//
// This file is part of the SST software package. For license
// information, see the LICENSE file in the top level directory of the
// distribution.


#ifndef _H_SST_DHO02
#define _H_SST_DHO02

#include <bitset>

#include <sst/core/event.h>
#include <sst/core/sst_types.h>
#include <sst/core/component.h>
#include <sst/core/link.h>
#include <sst/core/timeConverter.h>
#include <sst/core/output.h>
#include <sst/elements/memHierarchy/memEvent.h>
#include <sst/elements/memHierarchy/cacheListener.h>

#define MAXBITS 1024
typedef std::bitset<MAXBITS> signature;

using namespace SST;
using namespace SST::MemHierarchy;
using namespace std;

namespace SST {
namespace PhaseDetector {

struct lineTrack {
    bitset<8> touched; // currently hardcoded for 64B (8-word) lines
    SimTime_t entered; // when the line entered the cache
    uint64_t reads;
    uint64_t writes;

    lineTrack(SimTime_t now) : touched(0), entered(now), reads(0), writes(0) {;}
};

class Dho02 : public SST::MemHierarchy::CacheListener {
public:
    Dho02(ComponentId_t, Params& params);
    ~Dho02() {
        printf("Upon teardown, the phase detector reports\n  threshold: %f \n  window_len: %ld\n  sig_len: %d\n  drop_bits: %d\n  nphases: %d\n", threshold, window_len, sig_len, drop_bits, phase_table.size());
    };

    void notifyAccess(const CacheListenerNotification& notify);

    //void registerResponseCallback(Event::HandlerBase *handler);

    SST_ELI_REGISTER_SUBCOMPONENT_DERIVED(
        Dho02,
            "PhaseDetector",
            "Dho02",
            SST_ELI_ELEMENT_VERSION(1,0,0),
            "A working set-based phase detector",
            SST::MemHierarchy::CacheListener
    )

    SST_ELI_DOCUMENT_PARAMS(
        { "threshold",  "threshold for declaring phases similar",                 "0.5" },
        { "window_len", "Number of instructions in each window",                  "1e5" },
        { "sig_len",    "Size of the bitvector representing the phase signature", "1024" },
        { "stable_min", "The number of phases with similar signatures before we declare we have found a phase", "5" },
        { "drop_bits",  "Number of low bits to drop when hashing",                "3" },
    )

    SST_ELI_DOCUMENT_STATISTICS(
        { "hist_reads_log2", "Histogram of log2(cacheline reads before eviction)", "counts", 1 },
        { "hist_writes_log2", "Histogram of log2(cacheline write before eviction)", "counts", 1 },
        { "hist_age_log2", "Histogram of log2(cacheline ages before eviction)", "counts", 1 },
        { "hist_word_accesses", "Histogram of cacheline words accessed before eviction", "counts", 1 },
        { "evicts", "Number of evictions seen", "counts", 1 }
    )

private:
    uint64_t hash_ip(const Addr ip);
    float diff(signature sig1, signature sig2);
    std::hash<Addr> hashAddr;
    typedef unordered_map<Addr, lineTrack> cacheTrack_t;
    cacheTrack_t cacheLines;
    //std::vector<Event::HandlerBase*> registeredCallbacks;
    bool captureVirtual;
    Statistic<Addr>* rdHisto;
    Statistic<Addr>* wrHisto;
    Statistic<uint>* useHisto;
    Statistic<SimTime_t>* ageHisto;
    Statistic<uint>* evicts;

    // TODO: These only need to be set once. How do we make them const?
    float threshold;
    long window_len;
    int sig_len;
    int drop_bits;
    int stable_min;

    // TODO: These are currently initialized in the constructor. How do we move that here?
    int count;
    int stable;
    int phase;
    signature sig;
    signature last_sig;
    std::vector<signature> phase_table;

};

}
}

#endif
