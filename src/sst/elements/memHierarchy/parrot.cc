// Copyright 2013-2022 NTESS. Under the terlms
// of Contract DE-NA0003525 with NTESS, the U.S.
// Government retains certain rights in this software.
//
// Copyright (c) 2013-2022, NTESS
// All rights reserved.
//
// Portions are copyright of other developers:
// See the file CONTRIBUTORS.TXT in the top level directory
// of the distribution for more information.
//
// This file is part of the SST software package. For license
// information, see the LICENSE file in the top level directory of the
// distribution.

#include <sst_config.h>
#include <utility>
#include "parrot.h"
#include "memEventCustom.h"
#include "memEvent.h"
#include "../ariel/arielcore.h"

#include <boost/circular_buffer.hpp>
#include "ftpjrg.hpp"

#include <sst/core/params.h>
#include <sst/core/interfaces/stringEvent.h>
#include <typeinfo>

using namespace SST;
using namespace SST::MemHierarchy;
using namespace std;

Parrot::Parrot(ComponentId_t id, Params &params) : Component(id) {
    /* Setup output and debug streams */
    output.init("", 1, 0, Output::STDOUT);

    int debugLevel = params.find<int>("debug_level", 0);
    debug.init("", debugLevel, 0, (Output::output_location_t)params.find<int>("debug", 0));

    std::vector<Addr> addrArr;
    params.find_array<Addr>("debug_addr", addrArr);
    for (std::vector<Addr>::iterator it = addrArr.begin(); it != addrArr.end(); it++)
        DEBUG_ADDR.insert(*it);

    forward = params.find<bool>("forward", false);

    enableTracing = params.find<bool>("enable_tracing", false);
    if(enableTracing) {
        traceFile = params.find<std::string>("trace_prefix", getName()) + ".latency_trace.out";
        traceFileStream.open(traceFile);
        traceFileStream << "ip phase rwf threadID addr latency_nano\n";
    }

    enableMF = params.find<bool>("enable_multifidelity", false);

    if (enableMF && enableTracing) {
        stableRegionFile = params.find<std::string>("trace_prefix", getName()) + ".stable_region.out";
        stableRegionFileStream.open(stableRegionFile);
        stableRegionFileStream << "phase stable_start stable_size\n";
    }

    numAccesses = 0;
    haveRR = false;
    rrString = params.find<std::string>("rr_temp", "");
    completeRR[-1] = false;
    rng = new RNG::MersenneRNG(rng_seed);

    if (!rrString.empty()) {
        printf("rrString: %s\n", rrString.c_str());
        haveRR = true;
        std::istringstream iss(rrString);
        std::string benchmarkName;
        iss >> rrFile >> benchmarkName;
        printf("Getting RRs: [%s] [%s]\n", rrFile.c_str(), benchmarkName.c_str());
        std::ifstream rrFileStream(rrFile);
        if (!rrFileStream.good()) {
            output.fatal(CALL_INFO, 1, "Bad RR file");
        }

        std::string line, rrBench, rrPID, rrStart, rrEnd;
        while (getline(rrFileStream, line)) {
            std::istringstream iss(line);
            iss >> rrBench >> rrPID >> rrStart >> rrEnd;
            if (benchmarkName.compare(rrBench)){
                continue;
            }

            rrMap[std::stoi(rrPID)] = std::pair<int,int>(std::stoi(rrStart), std::stoi(rrEnd));
            rrRegion[std::stoi(rrPID)] = new std::vector<SimTime_t>();
            completeRR[std::stoi(rrPID)] = false;
        }
        if (rrMap.size() == 0) {
            //printf("[RR] Didn't find anything.\n");
            haveRR = false;
        } else {
            //printf("[RR] printing map...\n");
        }

        /*
        for (auto const& x : rrMap) {
            printf("%d: (%d, %d)\n", x.first, std::get<0>(x.second), std::get<1>(x.second));
        }
        */
    }

    if (haveRR && enableMF) {
        output.fatal(CALL_INFO, 1, "Do not use manual RR's and Multifidelity simulation simultaneously!");
    }

    /* Setup clock */
    clockHandler = new Clock::Handler<Parrot>(this, &Parrot::tick);
    clock = registerClock(params.find<std::string>("clock", "1GHz"), clockHandler);
    clockOn = true;
    timestamp = 0;

    /* Setup up links */
    if (isPortConnected("high_network_0")) {
        SST::Link *link = configureLink("high_network_0", "50ps", new Event::Handler<Parrot, unsigned int>(this, &Parrot::handleRequest, 0));
        if (!link)
            output.fatal(CALL_INFO, -1, "%s, Error: unable to configure link on port high_network_0.\n", getName().c_str());
        upLinks.push_back(link);
    } else {
        output.fatal(CALL_INFO, -1, "%s, Error: no connected 'high_network_0' port.\n", getName().c_str());
    }
    int num_links = 1;
    std::string linkname = "high_network_1";
    while (isPortConnected(linkname)) {
        SST::Link *link = configureLink(linkname, "50ps", new Event::Handler<Parrot, unsigned int>(this, &Parrot::handleRequest, num_links));
        if (!link)
            output.fatal(CALL_INFO, -1, "%s, Error: unable to configure link on port '%s'\n", getName().c_str(), linkname.c_str());
        upLinks.push_back(link);
        num_links++;
        linkname = "high_network_" + std::to_string(num_links);
    }

    /* Setup down links */
    if (isPortConnected("low_network_0")) {
        SST::Link *link = configureLink("low_network_0", "50ps", new Event::Handler<Parrot>(this, &Parrot::handleResponse));
        if (!link)
            output.fatal(CALL_INFO, -1, "%s, Error: unable to configure link on port low_network_0.\n", getName().c_str());
        downLinks.push_back(link);
    } else {
        output.fatal(CALL_INFO, -1, "%s, Error: no connected 'low_network_0' port.\n", getName().c_str());
    }

    for ( int i = 1; i < num_links; i++) {
        std::string linkname = "low_network_" + std::to_string(i);
        if (isPortConnected(linkname)) {
            SST::Link *link = configureLink(linkname, "50ps", new Event::Handler<Parrot>(this, &Parrot::handleResponse));
            if (!link)
                output.fatal(CALL_INFO, -1, "%s, Error: unable to configure link on port '%s'\n", getName().c_str(), linkname.c_str());
            downLinks.push_back(link);
        } else {
            output.fatal(CALL_INFO, -1, "%s, Error: Expected %d low_network links but port '%s' not connected.\n", getName().c_str(), num_links, linkname.c_str());
        }
    }

    /* Self link */
    selfLink = configureSelfLink("Self", "1 ns", new Event::Handler<Parrot>(this, &Parrot::handleResponse));

    /* Setup throughput limiting */
    requestsPerCycle = params.find<uint64_t>("requests_per_cycle", 0);
    responsesPerCycle = params.find<uint64_t>("responses_per_cycle", 0);

    /* Statistics */
    statAddr       = registerStatistic<Addr>("Addr");
    statWriteAddr  = registerStatistic<Addr>("WriteAddr");
    statReadAddr   = registerStatistic<Addr>("ReadAddr");
    statLatency    = registerStatistic<SimTime_t>("Latency");

    currentPhase = -1;
}

Parrot::~Parrot() {
    if(enableTracing) {
        traceFileStream.close();
    }

    if(enableMF && enableTracing) {
        for (const auto& [phase_id, phase_obj]: phase_map) {
            if (phase_obj.state == ps_stable) {
                stableRegionFileStream << phase_id << " " << phase_obj.stable_start << " " <<phase_obj.stable_size << '\n';
            } else {
                stableRegionFileStream << phase_id << " 0 0\n";
            }
        }
        stableRegionFileStream.close();
    }

    while (requestQueue.size()) {
        delete requestQueue.front();
        requestQueue.pop();
    }
    while (responseQueue.size()) {
        delete responseQueue.front();
        responseQueue.pop();
    }
}

void Parrot::handleRequest(SST::Event * ev, unsigned int threadid) {
    MemEvent *event = static_cast<MemEvent*>(ev);
    if (!clockOn) enableClock();
    //TODO: Optimize - give each link pair its own map

    // Detect phase messages
    if (event->getCmd() == Command::CustomReq) {
        CustomMemEvent *cme = static_cast<CustomMemEvent*>(ev);
        ArielCore::PhaseData *pd = static_cast<ArielCore::PhaseData*>(cme->getCustomData());
        lastPhase = currentPhase;
        currentPhase = pd->phase;
        //printf("Parrot has recieved a phase message of %d\n", pd->phase);

        if (enableMF) {
            // Check for phase boundary
            if (lastPhase != currentPhase) {
                if (debugMF) std::cout << "DebugMF: Phanse boundary identified: [ " << lastPhase << " -> " << currentPhase << " ]\n";
                // Give up on training if the phase ended before we could find a stable region
                if (lastPhase != -1) {
                    if (debugMF) std::cout << "DebugMF: Transitioned without reaching stability for phase (" << lastPhase << ")\n";
                    if (phase_map[lastPhase].state == ps_collect) {
                        phase_map[lastPhase].state = ps_giveup;
                    }
                }
                // If this is the first time we are seeing the new phase, add it to the map
                if ((currentPhase != -1) && (phase_map.find(currentPhase)==phase_map.end())) {
                    if (debugMF) std::cout << "DebugMF: New phase identified (" << currentPhase << ")\n";
                    //TODO: we just found a new phase so that means the last interval can be added to the latency history of this phase
                    // (1) need to collect the latencies (2) need to know the interval len, currently only in the PD
                    Phase p;
                    phase_map[currentPhase] = p;
                }
            }
        }
    } else {
        // Non phase messages here
        /*
        MemEvent *event = static_cast<MemEvent*>(ev); // should be safe - only read, write and flush are sent by Ariel. will likely break other CPUs
        Addr addr = event->getAddr();
        statAddr->addData(addr);
        */
    }

    // Foward regular messages, and forward phase messages only if forward is set
    // User is expected to set forward=False if this is the lowest Parrot
    // in the hierarchy
    // Handle non-phase messages
    if (event->getCmd() != Command::CustomReq) {
        //For now, go ahead and send a response
        if (haveRR && completeRR[currentPhase]) {
            uint32_t rdm_idx = rng->generateNextUInt32();
            rdm_idx = rdm_idx % (rrRegion[currentPhase])->size();
            // factor converts ns to cycles
            SimTime_t delay = (*rrRegion[currentPhase])[rdm_idx]-1; // subtract 1 for 1ns link latency
            delay = delay < 0 ? 0 : delay; // min is 0 cycles
            selfLink->send(delay, event->makeResponse());

        } else if ((enableMF) && (currentPhase!=-1) && (phase_map[currentPhase].state == ps_stable)) {
            // If we are doing MF, and in a phase, and the phase is stable, then sample
            std::vector<uint64_t>& rr = phase_map[currentPhase].rr;
            uint32_t rdm_idx = rng->generateNextUInt32();
            rdm_idx = rdm_idx % rr.size();
            SimTime_t delay = rr[rdm_idx]-1; // subtract 1 for 1ns link latency
            delay = delay < 0 ? 0 : delay; // min is 0 cycles
            selfLink->send(delay, event->makeResponse());

        } else {
            // Regular response
            threadRequestMap.insert(std::make_pair(event->getID(), std::make_pair(threadid, getCurrentSimTimeNano())));
            requestQueue.push(event);
        }


        //selfLink->send(event->makeResponse());
    } else if (forward) {
        // Handle phases when forwarding, only phase messages forwarded for now
        requestQueue.push(event);
    } else {
        delete ev;
    }
    /*
    if (forward || event->getCmd() != Command::CustomReq) {
        if (event->getCmd() != Command::CustomReq) {
            threadRequestMap.insert(std::make_pair(event->getID(), threadid));
            selfLink->send(event->makeResponse());
        }
        //requestQueue.push(event);

    } else {
        delete ev;
    }
    */



    //TODO: don't put phase messages in queue if forward is false
    //threadRequestMap.insert(std::make_pair(event->getID(), threadid));
    //requestQueue.push(event);
}

void Parrot::handleResponse(SST::Event * ev) {
    MemEventBase *event = static_cast<MemEventBase*>(ev);
    if (!clockOn) enableClock();
    responseQueue.push(event);
}

bool Parrot::tick(SST::Cycle_t cycle) {
    timestamp++;

    uint64_t sendcount = (requestsPerCycle == 0) ? requestQueue.size() : requestsPerCycle;

    /* Drain request queue */
    while (!requestQueue.empty() && sendcount > 0) {
        MemEventBase * event = requestQueue.front();
        //printf("Need to send event\n");
        unsigned int linkid = threadRequestMap.find(event->getID())->second.first; //second gets us the val, first gets the threadid

        // Requests that don't need a response, flushes and phase messages in our case, don't need to
        // stick around in this map
        if (event->queryFlag(MemEventBase::F_NORESPONSE)) {
            threadRequestMap.erase(event->getID());
        }

        downLinks[linkid]->send(event);
        //printf("sent event on %u\n", linkid);
        requestQueue.pop();
        sendcount--;
    }

    sendcount = (responsesPerCycle == 0) ? responseQueue.size() : responsesPerCycle;

    /* Drain response queue */
    while (!responseQueue.empty() && sendcount > 0) {
        MemEventBase * event = responseQueue.front();
        responseQueue.pop();

        if (threadRequestMap.find(event->getResponseToID()) == threadRequestMap.end()) {
            printf("Hmm, couldn't find that event");
        }

        std::pair<unsigned int, SimTime_t> req_data = threadRequestMap.find(event->getResponseToID())->second;
        unsigned int linkid = req_data.first;
        SimTime_t start_time = req_data.second;
        threadRequestMap.erase(event->getResponseToID());
        upLinks[linkid]->send(event);

        auto latency = getCurrentSimTimeNano() - start_time;
        statLatency->addData(latency);

        numAccesses++;

        // Multi-fidelity functionality
        // We are in the response queue here. If we are in a phase and still collecting data, then collect it
        if ((enableMF) && (currentPhase != -1) && (phase_map[currentPhase].state == ps_collect)) {
            Phase& cur = phase_map[currentPhase];
            cur.history.push_back(latency);
            if (cur.history.size() > mf_data_needed) {
                if (debugMF) std::cout << "DebugMF: Running FtPjRG on phase (" << currentPhase << ")\n";
                FtPjRG ft;
                auto [stable_start, stable_size, stable_found] = ft.run(cur.history);
                if (!stable_found) {
                    if (stable_start == 0) {
                        if (debugMF) std::cout << "DebugMF: Stable region not found. GIVE UP. (" << currentPhase << ")\n";
                        // If we couldn't even advance the window once, we will never find a stable phase
                        cur.state = ps_giveup;
                    } else {
                        if (debugMF) std::cout << "DebugMF: Stable region not found. TRY AGAIN. (" << currentPhase << ")\n";
                        // If the method failed to find a stable region, we can delete
                        // everything before the final starting position.
                        //cur.history.erase(cur.history.begin(), cur.history.begin() + cur.history.size()/2);
                        if (debugMF) std::cout << " -> FtPjRG return [" << stable_start << ", " << stable_size << "]\n";
                        if (debugMF) std::cout << " -> Removing first " << stable_start << " elements of cur\n";
                        cur.history.erase(
                            cur.history.begin(),
                            cur.history.begin() + stable_start);
                        cur.deleted_latencies += stable_start;
                        if (debugMF) std::cout << " -> New size is " << cur.history.size() << std::endl;
                    }
                } else {
                    // We found our stable phase. Store into a vector for faster sampling
                    if (debugMF) std::cout << "DebugMF: Stable region found. (" << currentPhase << ") (" << cur.deleted_latencies + stable_start << ", " << cur.deleted_latencies+stable_start+stable_size << ")\n";
                    cur.rr = std::vector<uint64_t>(cur.history.begin()+stable_start,
                                                 cur.history.begin()+stable_start+stable_size);
                    // We are done with the latency history
                    cur.history.clear();
                    cur.state = ps_stable;
                    cur.stable_start = stable_start;
                    cur.stable_size = stable_size;
                }
            }
            // Once it is long enough, we can try to find a stable region.
            // If stability checking works, we can move to state ps_stable.
            // If the phase ends and we are still in ps_collect, then we will move to ps_giveup;
            // Also, if it takes too long to find a stable region, we will move to ps_giveup.
        }

        // If we are in a phase, and RR is enabled, add to map
        if (haveRR){

            if (numAccesses == std::get<0>(rrMap[currentPhase])) {
                printf("[RR]: started tracing\n");
            }
            if ( (numAccesses >= std::get<0>(rrMap[currentPhase])) && (numAccesses < std::get<1>(rrMap[currentPhase])) ) {
                rrRegion[currentPhase]->push_back(latency);
            }
            if (numAccesses == std::get<1>(rrMap[currentPhase])) {
                printf("[RR]: stopped tracing\n");
                completeRR[currentPhase] = true;
            }
        }
        if (enableTracing) {
            MemEvent *me = static_cast<MemEvent*>(event);
            Command cmd = me->getCmd();

            // Don't worry about Flush responses
            if (cmd != Command::FlushAllResp && cmd != Command::FlushLineResp) {
                std::string rwf = "-";
                if (cmd == Command::GetSResp) {
                    rwf = "r";
                } else if (cmd == Command::WriteResp) {
                    rwf = "w";
                } else {
                    output.fatal(CALL_INFO, -1, "%s, Error: unexpected command in parrot reponse.\n", getName().c_str());
                }
                traceFileStream << me->getInstructionPointer() << " " <<
                                   currentPhase                << " " <<
                                   rwf                         << " " <<
                                   linkid                      << " " <<
                                   me->getAddr()               << " " <<
                                   latency                     << "\n";
            }
        }

        sendcount--;
    }

    /* Turn off clock if queues are empty */
    if (requestQueue.empty() && responseQueue.empty()) {
        clockOn = false;
        return true;
    }
    return false;
}

inline void Parrot::enableClock() {
    clockOn = true;
    timestamp = reregisterClock(clock, clockHandler);
    timestamp--;
}

/** SST init/finish */
void Parrot::setup() {}

void Parrot::finish() {}

/*
 *  Init:
 *      forward all CPU events to memory hierarchy
 *      Broadcast L1's 'SST::MemHierarchy::MemEvent' event to all CPUs
 *
 */
void Parrot::init(unsigned int phase) {
    SST::Event * ev;

    // Pass CPU events to memory hierarchy, generally these are memory initialization
    for (int i = 0; i < upLinks.size(); i++) {
        while ((ev = upLinks[i]->recvInitData()) != NULL) {
            MemEventInit * memEvent = dynamic_cast<MemEventInit*>(ev);
            if (memEvent) {
                downLinks[i]->sendInitData(memEvent->clone());
            }
            delete ev;
        }
    }

    for (int i = 0; i < downLinks.size(); i++) {
        while ((ev = downLinks[i]->recvInitData()) != NULL) {
            MemEventInit * memEvent = dynamic_cast<MemEventInit*>(ev);
            if (memEvent) {
                upLinks[i]->sendInitData(memEvent->clone());
            }
            delete ev;
        }
    }
}

