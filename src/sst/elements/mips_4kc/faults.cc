// Copyright 2020 NTESS. Under the terms
// of Contract DE-NA0003525 with NTESS, the U.S.
// Government retains certain rights in this software.
//
// Copyright (c) 2020 NTESS
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
#include "faults.h"
#include <fstream>
#include <sst/core/rng/mersenne.h>
#include "mips_4kc.h"

using namespace SST;
using namespace SST::RNG;
using namespace SST::MIPS4KCComponent;
using namespace faultTrack;

const std::map<std::string, faultChecker_t::location_idx_t> faultChecker_t::parseMap = {
    {"RF", RF_FAULT_IDX},
    {"ID", ID_FAULT_IDX},
    {"MDU", MDU_FAULT_IDX},
    {"MEM_PRE", MEM_PRE_FAULT_IDX},
    {"MEM_POST", MEM_POST_FAULT_IDX},
    {"WB", WB_FAULT_IDX},
    {"ALU", ALU_FAULT_IDX}
};

void faultChecker_t::init(faultTrack::location_t loc, uint64_t period, string fault_file,
                          uint32_t seed, Output *Out) {
    out = Out;

    //init RNG
    if (seed != 0) {
        rng = new MersenneRNG(seed);
    } else {
        rng = new MersenneRNG();
    }

    // "Old Style" (non-file) fault injection
    faultTrack::location_t locations = loc;
    printf("Fault Injector: Inject faults at 0x%x\n", locations);


    for (int i = NO_LOC_FAULT_IDX; i < LAST_FAULT_IDX; ++i) {
        event_count[i] = 0;
    }

    // randomly generate when different faults should occur
#define GEN_F_TIME(FAULT) \
    if (locations & FAULT) {                                            \
        faultTime[FAULT##_IDX] = rng->generateNextUInt64() % period;     \
        printf(" Will Inject %s at %lld\n", #FAULT ,faultTime[FAULT##_IDX]); \
    } else {                                                            \
        faultTime[FAULT##_IDX] = -1;                                     \
    }

    GEN_F_TIME(RF_FAULT);
    GEN_F_TIME(ID_FAULT);
    GEN_F_TIME(MDU_FAULT);
    GEN_F_TIME(MEM_PRE_FAULT);
    GEN_F_TIME(MEM_POST_FAULT);
    GEN_F_TIME(WB_FAULT);
    GEN_F_TIME(ALU_FAULT);

#undef GEN_F_TIME

    // "New Sytle" (file-based) faultsread in file of faults
    if (fault_file != "") {
        readFaultFile(fault_file);
    }
}

// take a string, return its location index or LAST_FAULT_IDX for error
faultChecker_t::location_idx_t faultChecker_t::findLocation(const string& loc) {
    const auto &iter = parseMap.find(loc);
    if (iter != parseMap.end()) {
        return iter->second;
    } else {
        return LAST_FAULT_IDX;
    }
}

// read in one line, add to appropriate 
bool faultChecker_t::readFaultFileLine(ifstream &in, const int lineNum) {

    string firstWord;
    in >> firstWord;

    if (in.good()) {
        if (firstWord.find("#") != std::string::npos) {
            // comment line
            string commentLine;
            getline(in, commentLine);
            printf("COMMENT: %s\n", commentLine.c_str());
        } else {
            // regular fault line
            location_idx_t loc = findLocation(firstWord);
            printf("Fault Loc %s (%d), ", firstWord.c_str(), loc);
            if (loc == LAST_FAULT_IDX) {
                out->fatal(CALL_INFO,-1, "Invalid Fault Location (\"%s\") Line %d\n", 
                           firstWord.c_str(), lineNum);
            }

            string trigger;
            bool cycle_trigger = 0;
            in >> trigger;

            if (!in.good()) {
                out->fatal(CALL_INFO,-1, "Fault File Error Line %d\n", lineNum);
            } else {
                std::transform(trigger.begin(), trigger.end(), trigger.begin(),
                               [](unsigned char c){ return std::tolower(c); });

                if (trigger == "cycle") {
                    printf("Cycle Trigger ");
                    cycle_trigger = 1;
                } else if (trigger == "event") {
                    printf("Event Trigger ");
                    cycle_trigger = 0;
                } else {
                    out->fatal(CALL_INFO,-1, "Invalid Fault Trigger (\"%s\") Line %d\n", 
                           trigger.c_str(), lineNum);
                }
            }

            uint64_t when;
            readNum(when, in, lineNum);

            uint32_t what;
            readNum(what, in, lineNum);

            printf("@ %llu flip: 0x%x\n", when, what);

            // put in data
            if (cycle_trigger) {
                upcomingFaults_c[loc].push_back(faultFileDesc(when, what));
            } else {
                upcomingFaults_e[loc].push_back(faultFileDesc(when, what));
            }
        }

        return true; // more to do (probably)
    } else if (in.eof()) {
        return false; //we're done
    } else {
        out->fatal(CALL_INFO,-1, "fault file error. Line %d\n", lineNum);
        return false;
    }
}

void faultChecker_t::readFaultFile(string fault_file_path) {

    // open file
    ifstream f_file;
    f_file.open(fault_file_path);
    if (!f_file.is_open() || !f_file.good()) {
        out->fatal(CALL_INFO,-1, "Cannot Open fault file: %s\n", fault_file_path.c_str());
    }

    // read lines/comments, inserting into upcomingFaults_x
    bool moreToRead = true;
    int n = 0;
    while(moreToRead) {
        moreToRead = readFaultFileLine(f_file, ++n);
    }

    // close file
    f_file.close();

    // sort upcomingFaults_x
    for (int i = NO_LOC_FAULT_IDX; i < LAST_FAULT_IDX; ++i) {
        sort(upcomingFaults_c[i].begin(), upcomingFaults_c[i].end(), 
             [](const faultFileDesc& a, const faultFileDesc& b) -> bool {
                 return a.when > b.when;
             });
        sort(upcomingFaults_e[i].begin(), upcomingFaults_e[i].end(), 
             [](const faultFileDesc& a, const faultFileDesc& b) -> bool {
                 return a.when > b.when;
             });
    }
}

bool faultChecker_t::checkForNewStyleFault(location_idx_t idx) {
    bool ret = false;
    uint32_t what=0;

    // check for fault @ cycle
    if (!upcomingFaults_c[idx].empty() && 
        reg_word::getNow() == upcomingFaults_c[idx].back().when) {        
        ret = true;
        what = upcomingFaults_c[idx].back().what;
        upcomingFaults_c[idx].pop_back();
    }

    // check for fault @ event
    if (!upcomingFaults_e[idx].empty() && 
        event_count[idx] == upcomingFaults_e[idx].back().when) {
        ret = true;
        what = upcomingFaults_e[idx].back().what;
        upcomingFaults_e[idx].pop_back();
    }

    return ret;
}

// should we inject?
bool faultChecker_t::checkForFault(faultTrack::location_t loc) {
    location_idx_t newLoc = LAST_FAULT_IDX; 

    // advances event counts and check for "old style" faults 
    switch (loc) {
    case RF_FAULT:
        newLoc = RF_FAULT_IDX;
        event_count[RF_FAULT_IDX]++;
        if (reg_word::getNow() == faultTime[RF_FAULT_IDX]) {return true;}
        break;
    case ID_FAULT:
        // ???
        newLoc = ID_FAULT_IDX;
        event_count[ID_FAULT_IDX]++;
        break;
    case MDU_FAULT:
        newLoc = MDU_FAULT_IDX;
        event_count[MDU_FAULT_IDX]++;
        if(event_count[MDU_FAULT_IDX] == faultTime[MDU_FAULT_IDX]) {
            return true;
        }
        break;
    case MEM_PRE_FAULT:
        newLoc = MEM_PRE_FAULT_IDX;
        // this assumes that MEM_PRE must be checked everytime since
        // we don't increment in MEM_POST
        event_count[MEM_PRE_FAULT_IDX]++;
        if(event_count[MEM_PRE_FAULT_IDX] == faultTime[MEM_PRE_FAULT_IDX]) {return true;}
        break;
    case MEM_POST_FAULT:
        newLoc = MEM_POST_FAULT_IDX;
        event_count[MEM_POST_FAULT_IDX]++;
        if(event_count[MEM_PRE_FAULT_IDX] == faultTime[MEM_POST_FAULT_IDX]) {return true;}
        break;
    case WB_FAULT:
        newLoc = WB_FAULT_IDX;
        event_count[WB_FAULT_IDX]++;
        if (reg_word::getNow() == faultTime[WB_FAULT_IDX]) {return true;}
        break;
    case ALU_FAULT:
        newLoc = ALU_FAULT_IDX;
        event_count[ALU_FAULT_IDX]++;
        if (reg_word::getNow() == faultTime[ALU_FAULT_IDX]) {return true;}
        break;
    default:
        printf("Unknown fault location\n");
    }

    // check for New Style (file-based) faults
    bool ret = checkForNewStyleFault(newLoc);

    // default
    return ret;
}

// handy for picking which register in register file
unsigned int faultChecker_t::getRand1_31() {
    unsigned int ret;
    do {
        ret = rng->generateNextUInt32() & 0x1f;
    } while (ret == 0);
    return ret;
}

faultDesc faultChecker_t::getFault(faultTrack::location_t loc) {
    int bit = rng->generateNextUInt32() & 0x1f;
    return faultDesc(loc, bit);
}

void faultChecker_t::checkAndInject_RF(reg_word R[32]) {
    if(checkForFault(faultTrack::RF_FAULT)) {
        unsigned int reg = getRand1_31();
        printf("INJECTING RF FAULT reg %d @ %lld\n", reg, reg_word::getNow());
        R[reg].fault(getFault(RF_FAULT));
    }
}


void faultChecker_t::checkAndInject_MDU(reg_word &hi, reg_word &lo) {
    if (checkForFault(MDU_FAULT)) {
        uint32_t roll = rng->generateNextUInt32();
        bool faultHi = roll & 0x1;
        roll >>= 1;

        int bit = roll & 0x1f;

        printf("INJECTING MDU FAULT reg %s:%d @ %lld\n", 
               (faultHi) ? "hi" : "lo", bit, reg_word::getNow());
        if (faultHi) {
            hi.fault(faultDesc(MDU_FAULT, bit));
        } else {
            lo.fault(faultDesc(MDU_FAULT, bit));
        }
    }
}

// possibly fault data and address
// isLoad ignored for now
void faultChecker_t::checkAndInject_MEM_PRE(reg_word &addr, 
                                            reg_word &value, bool isLoad) {
    if (checkForFault(MEM_PRE_FAULT)) {
        uint32_t roll = rng->generateNextUInt32();
        bool faultAddr = roll & 0x1;
        roll >>= 1;

        int bit = roll & 0x1f;

        printf("INJECTING MEM_PRE FAULT %s:%d %s @ %lld\n", 
               (faultAddr) ? "Address" : "Data", bit,
               (isLoad) ? "(isLoad)" : "(isStore)",
               reg_word::getNow());
        if (isLoad && !faultAddr) {
            printf(" INJECTING fault to data on load: no effect\n");
        }
        if (faultAddr) {
            addr.fault(faultDesc(MEM_PRE_FAULT, bit));
        } else {
            value.fault(faultDesc(MEM_PRE_FAULT, bit));
        }
    }
}

// inject fault after MEM. Note: if inject into 'store' may have no
// value
void faultChecker_t::checkAndInject_MEM_POST(reg_word &data) {
    if(checkForFault(MEM_POST_FAULT)) {
        printf("INJECTING MEM_POST Fault @ %lld\n", reg_word::getNow());
        data.fault(getFault(MEM_POST_FAULT));
    }
}

void faultChecker_t::checkAndInject_WB(reg_word &data) {
    if(checkForFault(WB_FAULT)) {
        printf("INJECTING WB Fault  @ %lld\n", reg_word::getNow());
        data.fault(getFault(WB_FAULT));
    }
}

void faultChecker_t::checkAndInject_ALU(reg_word &data) {
    if(checkForFault(ALU_FAULT)) { 
        printf("INJECTING ALU Fault  @ %lld\n", reg_word::getNow());
        data.fault(getFault(ALU_FAULT));
    } 
}


void faultChecker_t::printStats() {
    printf("Faultable Events:\n");

#define PF(STR) printf("\t%s_event : %llu\n", #STR, event_count[STR##_IDX]);
    
    PF(RF_FAULT);
    PF(ID_FAULT);
    PF(MDU_FAULT);
    PF(MEM_PRE_FAULT);
    PF(MEM_POST_FAULT);
    PF(WB_FAULT);
    PF(ALU_FAULT);

#undef PF

}
