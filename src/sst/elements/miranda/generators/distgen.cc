// Copyright 2009-2019 NTESS. Under the terms
// of Contract DE-NA0003525 with NTESS, the U.S.
// Government retains certain rights in this software.
//
// Copyright (c) 2009-2019, NTESS
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
#include <sst/core/params.h>
#include <sst/core/rng/marsaglia.h>
#include <sst/core/rng/expon.h>
#include <sst/elements/miranda/generators/distgen.h>

using namespace SST::Miranda;

DistGenerator::DistGenerator( ComponentId_t id, Params& params ) : RequestGenerator(id, params) {
    build(params);
}

DistGenerator::DistGenerator( Component* owner, Params& params ) : RequestGenerator(owner, params) {
    build(params);
}

void DistGenerator::build(Params &params) {        
    const uint32_t verbose = params.find<uint32_t>("verbose", 0);
        
    out = new Output("DistGenerator[@p:@l]: ", verbose, 0, Output::STDOUT);

    iterations = params.find<uint64_t>("iterations", 1);
    issueCount = (params.find<uint64_t>("count", 1000));
    maxCount = (params.find<uint64_t>("count", 1000));
    reqLength  = params.find<uint64_t>("length", 8);
    memStart    = params.find<uint64_t>("min_address", 0);
    memLength   = params.find<uint64_t>("max_address", 524288) - memStart;
    seed_a     = params.find<uint64_t>("seed_a", 11);
    seed_b     = params.find<uint64_t>("seed_b", 31);
    lambda     = params.find<double>("lambda", 0.05);
    rng        = new MarsagliaRNG(seed_a, seed_b);
    rng_expon  = new SSTExponentialDistribution(lambda, rng);
    last_addr = memStart;
    maxPhase = 2;
    phase = maxPhase;

    out->verbose(CALL_INFO, 1, 0, "Will issue %" PRIu64 " operations\n", issueCount);
    out->verbose(CALL_INFO, 1, 0, "Request lengths: %" PRIu64 " bytes\n", reqLength);
    out->verbose(CALL_INFO, 1, 0, "Minimum address: %" PRIu64 "\n", memStart);
    out->verbose(CALL_INFO, 1, 0, "Maximum address: %" PRIu64 "\n", memStart + memLength);
    
    issueOpFences = params.find<std::string>("issue_op_fences", "yes") == "yes";
}

DistGenerator::~DistGenerator() {
    delete out;
    delete rng;
}

void DistGenerator::generate(MirandaRequestQueue<GeneratorRequest*>* q) {

    uint64_t addr = 0;

    if ( phase == 2 ) { // Uniform stride-1
        addr = ((maxCount - issueCount) % ( memLength / reqLength ) );
        addr *= reqLength; 
        addr += memStart;
    } else if (phase == 1) { // Exponential delta
        const uint64_t delta = rng_expon->getNextDouble();
        out->verbose(CALL_INFO, 4, 0, "Patrick: delta is : %" PRIu64 "\n", delta);
        addr = ((last_addr - memStart)/reqLength + delta) % (memLength / reqLength);
        addr *= reqLength;
        addr += memStart;
    } else { // Uniform random
        const uint64_t rand_addr = rng->generateNextUInt64();
        addr = (rand_addr % ( memLength / reqLength ) );
        addr *= reqLength; 
        addr += memStart;
    }
    last_addr = addr;
    out->verbose(CALL_INFO, 4, 0, "Generating next request number: %" PRIu64 " at address %" PRIu64 "\n", issueCount, addr);

    MemoryOpRequest* readAddr = new MemoryOpRequest(addr, reqLength, READ);

    // Add a little "noise" to the IP
    uint64_t noise = issueCount % 20;

    if ( phase == 2) {
        readAddr->setInstrPtr(0x100 + noise);
    } else if (phase == 1) {
        readAddr->setInstrPtr(0x200 + noise);
    } else {
        readAddr->setInstrPtr(0x300 + noise);
    }

    q->push_back(readAddr);

    issueCount--;

    if ( issueCount == 0 && phase == 0 && --iterations != 0) {
        phase = maxPhase;
        issueCount = maxCount;
    }
    else if (issueCount == 0 && phase != 0) {
        issueCount = maxCount;
        last_addr = memStart;
        phase--;
    }


}

bool DistGenerator::isFinished() {

    return (issueCount == 0 && phase == 0);
}

void DistGenerator::completed() {

}
