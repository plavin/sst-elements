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

// File:   addrHistogrammer.cc
// Author: Patrick Lavin (derived from Cassini prefetcher modules written by Si Hammond)

#include "sst_config.h"
#include "Dho02.h"

#include <stdint.h>

#include <algorithm>

#include "sst/core/params.h"
#include <sst/core/unitAlgebra.h>

using namespace SST;
using namespace SST::MemHierarchy;
using namespace SST::PhaseDetector;

static const int tab64[64] = {
    63,  0, 58,  1, 59, 47, 53,  2,
    60, 39, 48, 27, 54, 33, 42,  3,
    61, 51, 37, 40, 49, 18, 28, 20,
    55, 30, 34, 11, 43, 14, 22,  4,
    62, 57, 46, 52, 38, 26, 32, 41,
    50, 36, 17, 19, 29, 10, 13, 21,
    56, 45, 25, 31, 35, 16,  9, 12,
    44, 24, 15,  8, 23,  7,  6,  5};

static int log2_64(uint64_t value)
{
    value |= value >> 1;
    value |= value >> 2;
    value |= value >> 4;
    value |= value >> 8;
    value |= value >> 16;
    value |= value >> 32;
    return tab64[((uint64_t)((value - (value >> 1))*0x07EDD5E59A4E28C2)) >> 58];
}



Dho02::Dho02(ComponentId_t id, Params& params) : CacheListener(id, params) {
    rdHisto = registerStatistic<Addr>("hist_reads_log2");
    wrHisto = registerStatistic<Addr>("hist_writes_log2");
    useHisto = registerStatistic<uint>("hist_word_accesses");
    ageHisto = registerStatistic<SimTime_t>("hist_age_log2");
    evicts = registerStatistic<uint>("evicts");

    threshold  = params.find<float>("threshold", 0.5);
    window_len = params.find<long>("window_len", 1e5);
    sig_len    = params.find<int>("sig_len", 1024);
    drop_bits  = params.find<int>("drop_bits", 3);

    if (sig_len > sig.size()) {
        printf("Error in Dho02.cc: bitset too small: increase MAXBITS in Dho02.h\n");
        exit(1);
    }
    count = 0;
    stable = 0;
    sig.reset();
    last_sig.reset();
}

uint64_t Dho02::hash_ip(const Addr ip) {
    return hashAddr(ip >> drop_bits) >> (64 - log2_64(sig_len));
}

float Dho02::diff(const signature sig1, const signature sig2) {
    return ((float)((sig1 ^ sig2).count())) / (sig1 | sig2).count();
}

void Dho02::notifyAccess(const CacheListenerNotification& notify) {
    const NotifyAccessType notifyType = notify.getAccessType();
    const NotifyResultType notifyResType = notify.getResultType();

    Addr addr = notify.getTargetAddress(); // target address
    Addr cacheAddr = notify.getPhysicalAddress(); // cacheline (base) address

    // TODO: Need IP. Is that what this is?
    sig[hash_ip(notify.getVirtualAddress())] = 1;
    count++;

    if (count % window_len == 0) {
        if (diff(sig, last_sig) < threshold) {
            stable += 1;
            if (stable >= stable_min && phase == -1) {
                phase_table.push_back(sig);
                phase = phase_table.size() - 1;
            }
        } else {
            stable = 0;
            phase = -1;

            if (phase_table.size() > 0) {
                std::vector<float> similar;
                for (auto & s : phase_table) {
                    similar.push_back(diff(sig, s));
                }
                size_t best = std::distance(similar.begin(),
                        std::max_element(similar.begin(), similar.end()));
                if (similar[best] < threshold) {
                    phase = best;
                }
            }
        }

        last_sig = sig;
        sig.reset();
    }
    /*
    switch (notifyType) {
    case READ:
    case WRITE:
        {
            auto iter = cacheLines.find(cacheAddr);
            if (iter == cacheLines.end()) {
                // insert a new one
                SimTime_t now = getSimulation()->getCurrentSimCycle();
                iter = (cacheLines.insert({cacheAddr, lineTrack(now)})).first;
            }
            // update
            if (notify.getSize() > 8) {
	      //printf("Not sure what to do here. access size > 8, %d\n", notify.getSize());
            }
            Addr offset = (addr - cacheAddr) / 8;
            iter->second.touched[offset] = 1;
            if (notifyType == READ) {
                iter->second.reads++;
            } else {
                iter->second.writes++;
            }
        }
        break;
    case EVICT:
        // find the cacheline record
        {
            auto iter = cacheLines.find(cacheAddr);
            if (iter != cacheLines.end()) {
                // record it
                rdHisto->addData(log2_64(iter->second.reads));
                wrHisto->addData(log2_64(iter->second.writes));
                SimTime_t now = getSimulation()->getCurrentSimCycle();
                ageHisto->addData(log2_64(now - iter->second.entered));
                uint touched = iter->second.touched.count();
                useHisto->addData(touched);
		evicts->addData(1);
                //delete it
                cacheLines.erase(iter);
            } else {
                // couldn't find record?
                printf("Not sure what to do here. Couldn't find record\n");
            }
        }
    case PREFETCH:
        break;
    default:
        printf("Invalid notify Type\n");
    }
    */
}

//void Dho02::registerResponseCallback(Event::HandlerBase *handler) {
//    registeredCallbacks.push_back(handler);
//}
