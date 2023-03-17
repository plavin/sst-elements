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

#ifndef _MEMHIERARCHY_PARROT_
#define _MEMHIERARCHY_PARROT_

#include <map>
#include <queue>

#include <sst/core/event.h>
#include <sst/core/sst_types.h>
#include <sst/core/component.h>
#include <sst/core/link.h>
#include <sst/core/timeConverter.h>
#include <sst/core/output.h>

#include "sst/elements/memHierarchy/memEventBase.h"
#include "sst/elements/memHierarchy/util.h"

using namespace std;

namespace SST { namespace MemHierarchy {

class Parrot : public Component {
public:
/* Element Library Info */
    SST_ELI_REGISTER_COMPONENT(Parrot, "memHierarchy", "Parrot", SST_ELI_ELEMENT_VERSION(1,0,0),
            "Layer that learns the memory behavior per-phase and mimics it", COMPONENT_CATEGORY_MEMORY)

    SST_ELI_DOCUMENT_PARAMS(
            {"clock",               "(string) Clock frequency or period with units (Hz or s; SI units OK).", NULL},
            {"requests_per_cycle",  "(uint) Number of requests to forward to L1 each cycle (for all threads combined). 0 indicates unlimited", "0"},
            {"responses_per_cycle", "(uint) Number of responses to forward to threads each cycle (for all threads combined). 0 indicates unlimited", "0"},
            {"debug",               "(uint) Where to print debug output. Options: 0[no output], 1[stdout], 2[stderr], 3[file]", "0"},
            {"debug_level",         "(uint) Debug verbosity level. Between 0 and 10", "0"},
            {"forward",             "(bool) Whether to forward phase messages to the next level", "false"},
            {"debug_addr",          "(comma separated uint) Address(es) to be debugged. Leave empty for all, otherwise specify one or more, comma-separated values. Start and end string with brackets",""} )

    SST_ELI_DOCUMENT_PORTS(
          {"low_network_%(port)d", "Link to lower levels", {"memHierarchy.MemEventBase"} },
          {"high_network_%(port)d", "Links to higher level", {"memHierarchy.MemEventBase"} } )

/* Begin class definition */
    /** Constructor & destructor */
    Parrot(ComponentId_t id, Params &params);
    ~Parrot();

    /** SST component basic functions */
    void setup(void);
    void init(unsigned int phase);
    void finish(void);

    /** Handles response from memory hierarchy, passes to correct thread's CPU */
    void handleResponse(SST::Event *event);

    /** Handles request from CPU, passes on to memory hierarchy */
    void handleRequest(SST::Event *event, unsigned int threadid);

    /** Clock handler - requests/responses are sent on cycle boundaries */
    bool tick(SST::Cycle_t cycle);

private:
    /** Output and debug */
    Output debug;
    Output output;
    std::set<Addr> DEBUG_ADDR;

    /** Links */
    vector<SST::Link*> upLinks;
    vector<SST::Link*> downLinks;

    /** Timestamp & clock control */
    uint64_t    timestamp;
    bool        clockOn;
    Clock::Handler<Parrot>*  clockHandler;
    TimeConverter* clock;

    /** Track outstanding requests for routing responses correctly */
    std::map<Event::id_type, unsigned int> threadRequestMap;

    /** Throughput control */
    uint64_t requestsPerCycle;
    uint64_t responsesPerCycle;
    std::queue<MemEventBase*> requestQueue;
    std::queue<MemEventBase*> responseQueue;

    /* Phase forwarding */
    bool forward;

    inline void enableClock();
};

}
}

#endif /* _MEMHIERARCHY_PARROT_ */
