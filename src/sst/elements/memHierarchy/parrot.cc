// Copyright 2013-2022 NTESS. Under the terms
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
#include "parrot.h"
#include "memEventCustom.h"
#include "memEvent.h"
#include "../ariel/arielcore.h"

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
        traceFile = params.find<std::string>("trace_file", getName() + ".out");
        traceFileStream.open(traceFile);
        traceFileStream << "phase rwf threadID addr latency_nano\n";
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
    selfLink = configureSelfLink("Self", "10 ns", new Event::Handler<Parrot>(this, &Parrot::handleResponse));

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
    MemEventBase *event = static_cast<MemEventBase*>(ev);
    if (!clockOn) enableClock();
    //TODO: Optimize - give each link pair its own map

    // Detect phase messages
    if (event->getCmd() == Command::CustomReq) {
        CustomMemEvent *cme = static_cast<CustomMemEvent*>(ev);
        ArielCore::PhaseData *pd = static_cast<ArielCore::PhaseData*>(cme->getCustomData());
        currentPhase = pd->phase;
        printf("Parrot has recieved a phase message of %d\n", pd->phase);
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
        threadRequestMap.insert(std::make_pair(event->getID(), std::make_pair(threadid, getCurrentSimTimeNano())));
        //auto *responseEvent = event->makeResponse();
        requestQueue.push(event);

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

        std::pair<unsigned int, SimTime_t> req_data = threadRequestMap.find(event->getResponseToID())->second;
        unsigned int linkid = req_data.first;
        SimTime_t start_time = req_data.second;
        threadRequestMap.erase(event->getResponseToID());
        upLinks[linkid]->send(event);

        auto latency = getCurrentSimTimeNano() - start_time;
        statLatency->addData(latency);
        if (enableTracing) {
            MemEvent *me = static_cast<MemEvent*>(event);
            Command cmd = me->getCmd();
            std::string rwf = "-";
            if (cmd == Command::GetSResp) {
                rwf = "r";
            } else if (cmd == Command::WriteResp) {
                rwf = "w";
            } else {
                output.fatal(CALL_INFO, -1, "%s, Error: unexpected command in parrot reponse.\n", getName().c_str());
            }
            traceFileStream << currentPhase << " " <<
                            rwf             << " " <<
                            linkid          << " " <<
                            me->getAddr()   << " " <<
                            latency         << "\n";
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

