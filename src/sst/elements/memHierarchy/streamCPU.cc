// Copyright 2009-2016 Sandia Corporation. Under the terms
// of Contract DE-AC04-94AL85000 with Sandia Corporation, the U.S.
// Government retains certain rights in this software.
//
// Copyright (c) 2009-2016, Sandia Corporation
// All rights reserved.
//
// Portions are copyright of other developers:
// See the file CONTRIBUTORS.TXT in the top level directory
// the distribution for more information.
//
// This file is part of the SST software package. For license
// information, see the LICENSE file in the top level directory of the
// distribution.

#include <sst_config.h>
#include "streamCPU.h"

#include <sst/core/element.h>
#include <sst/core/params.h>
#include <sst/core/simulation.h>
#include <sst/core/interfaces/stringEvent.h>
#include "memEvent.h"


using namespace SST;
using namespace SST::MemHierarchy;


streamCPU::streamCPU(ComponentId_t id, Params& params) :
    Component(id), rng(id, 13)
{
    uint32_t outputLevel = params.find<uint32_t>("verbose", 0);
    out.init("StreamCPU:@p:@l: ", outputLevel, 0, Output::STDOUT);

    // get parameters
    commFreq = params.find<int>("commFreq", -1);
    if (commFreq < 0) {
	out.fatal(CALL_INFO, -1,"couldn't find communication frequency\n");
    }
    
    maxAddr = params.find<uint32_t>("memSize", -1) -1;
    if ( !maxAddr ) {
        out.fatal(CALL_INFO, -1, "Must set memSize\n");
    }

    maxOutstanding = params.find<uint64_t>("maxOutstanding", 10);

    do_write = params.find<bool>("do_write", 1);

    numLS = params.find<int>("num_loadstore", -1);

    maxReqsPerIssue = params.find<uint32_t>("reqsPerIssue", 1);
    if (maxReqsPerIssue < 1) {
        out.fatal(CALL_INFO, -1, "Cannot issue less than one request per cycle...fix your input deck\n");
    }

    // tell the simulator not to end without us
    registerAsPrimaryComponent();
    primaryComponentDoNotEndSim();

    // configure out links
    mem_link = configureLink( "mem_link",
            new Event::Handler<streamCPU>(this,
                &streamCPU::
                handleEvent) );
    
    if (!mem_link) {
        out.fatal(CALL_INFO, -1, "Error creating mem_link\n");   
    }

    addrOffset = params.find<uint64_t>("addressoffset", 0);

    //set our clock
    std::string clockFreq = params.find<std::string>("clock", "1GHz");
    clockHandler = new Clock::Handler<streamCPU>(this, &streamCPU::clockTic);
    clockTC = registerClock(clockFreq, clockHandler);
    num_reads_issued = num_reads_returned = 0;

    // Start the next address from the offset
    nextAddr = addrOffset;
}

streamCPU::streamCPU() :
	Component(-1)
{
	// for serialization only
}


void streamCPU::init(unsigned int phase)
{
	if ( !phase ) {
		mem_link->sendInitData(new Interfaces::StringEvent("SST::MemHierarchy::MemEvent"));
	}
}

// incoming events are scanned and deleted
void streamCPU::handleEvent(Event *ev)
{
	//out.output("recv\n");
	MemEvent *event = dynamic_cast<MemEvent*>(ev);
	if (event) {
		// May receive invalidates.  Just ignore 'em.
		if ( event->getCmd() == Inv) return;

		std::map<MemEvent::id_type, SimTime_t>::iterator i = requests.find(event->getResponseToID());
		if ( i == requests.end() ) {
			out.fatal(CALL_INFO, -1, "Event (%" PRIx64 ", %d) not found!\n", event->getResponseToID().first, event->getResponseToID().second);
		} else {
			SimTime_t et = getCurrentSimTime() - i->second;
			requests.erase(i);

			out.verbose(CALL_INFO, 1, 0, "Received MemEvent (response to: %10" PRIu64 ", Addr=%15" PRIu64 ", Took: %7" PRIu64 "ns, %6lu pending requests).\n",
				event->getResponseToID().first, event->getAddr(), et, requests.size());
			num_reads_returned++;
		}

		delete event;
	} else {
		out.output("Error! Bad Event Type!\n");
	}
}

bool streamCPU::clockTic( Cycle_t )
{
    // communicate?
    if ((numLS != 0) && ((rng.generateNextUInt32() % commFreq) == 0) && requests.size() <= maxOutstanding) {
	// yes, communicate
	// create event
	// x8 to prevent splitting blocks
        uint32_t reqsToSend = 1;
        if (maxReqsPerIssue > 1) reqsToSend += rng.generateNextUInt32() % maxReqsPerIssue;
        if (reqsToSend > (maxOutstanding - requests.size())) reqsToSend = maxOutstanding - requests.size();
        if (reqsToSend > numLS) reqsToSend = numLS;

        for (int i = 0; i < reqsToSend; i++) {

    	    bool doWrite = do_write && (((rng.generateNextUInt32() % 10) == 0));

	    MemEvent *e = new MemEvent(this, nextAddr, nextAddr, doWrite ? GetX : GetS);
            e->setSize(4); // Load 4 bytes
	    if ( doWrite ) {
	        e->setPayload(4, (uint8_t*)&nextAddr);
	    }
	    
            mem_link->send(e);
            requests.insert(std::make_pair(e->getID(), getCurrentSimTime()));

	    out.verbose(CALL_INFO, 1, 0, "Issued request %10d: %5s for address %20d.\n", numLS, (doWrite ? "write" : "read"), nextAddr);

	    num_reads_issued++;
            nextAddr = (nextAddr + 8);

            if (nextAddr > (maxAddr - 4)) {
		nextAddr = addrOffset;
	    }

	    numLS--;
	}
    }

    if ( numLS == 0 && requests.size() == 0 ) {
        primaryComponentOKToEndSim();
        return true;
    }

    // return false so we keep going
    return false;
}


