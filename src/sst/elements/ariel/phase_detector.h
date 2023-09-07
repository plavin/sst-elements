// # Copyright (c) 2021, Georgia Institute of Technology
// #
// # SPDX-License-Identifier: Apache-2.0
// #
// # Licensed under the Apache License, Version 2.0 (the "License");
// # you may not use this file except in compliance with the License.
// # You may obtain a copy of the License at
// #
// #     http://www.apache.org/licenses/LICENSE-2.0
// #
// # Unless required by applicable law or agreed to in writing, software
// # distributed under the License is distributed on an "AS IS" BASIS,
// # WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// # See the License for the specific language governing permissions and
// # limitations under the License.


// Ryan Thomas Lynch
// Georgia Institute of Technology
// ryan.lynch@gatech.edu
// CRNCH Lab


// Phase detection header file
// Version 0.9

#ifndef PHASE_DETECTOR_H
#define PHASE_DETECTOR_H

#include <cstdio>
#include <cinttypes>
#include <iostream>

#include <memory>
#include <deque>
// #include <map>
#include <vector>
// #include <bits/stdc++.h>
#include <bitset>
#include <fstream>
//TODO: replace bits/stdc++ with something that actually works for other compilers?
// #include <bit>
// #include <functional>
#include <random>
// #include <pair>

using namespace std;


// double threshold = 0.5;
// int interval_len = 10000;
// const int signature_len = 1024;
// const int log2_signature_len = 10; // should be log 2 of the above value
// int drop_bits = 3;

// #define threshold 0.5
// #define interval_len 10000
// #define signature_len 1024
// #define log2_signature_len 10
// #define drop_bits 3

namespace phase_detector_constants {
    constexpr double threshold = 0.65; //modsim
    //constexpr uint64_t interval_len = 100'000; //spatter
    constexpr uint64_t interval_len = 200'000; //spatter
    //constexpr uint signature_len = 1024;
    constexpr uint signature_len = 1024;
    constexpr uint log2_signature_len = 10;
    constexpr uint drop_bits = 3;
}

using bitvec = bitset<phase_detector_constants::signature_len>;

typedef int64_t phase_id_type;

typedef void (*listener_function)(phase_id_type); //listener functor type that takes in a phase ID and returns nothing
//typedef void (SST::ArielComponent::ArielCore::*listener_function_ariel)(phase_id_type); //listener functor type that takes in a phase ID and returns nothing

typedef void (*dram_listener_function)(uint64_t, phase_id_type); //listener functor type that takes in a phase ID and returns nothing

void read_file(char const log_file[], int is_binary = 1);

void test_listener(phase_id_type current_phase);
void dram_phase_trace_listener(phase_id_type new_phase);
void register_dram_trace_listener(dram_listener_function f);

//used for reading from memtrace binary output files
struct _binary_output_x86_memtrace_struct_t {

    bool is_write;
    uint64_t virtual_address;
    uint64_t size_of_access;
    uint64_t instruction_pointer;

};

typedef struct _binary_output_x86_memtrace_struct_t binary_output_x86_memtrace_struct_t;



//format used for my new simplified/combined approach

struct _simple_ins_ref_t {
    unsigned char* pc;
    int opcode;
    int is_memory_ref;
    int is_cond_branch;
};

typedef struct _simple_ins_ref_t simple_ins_ref_t;

template <std::size_t N>
class bitvec2 {
    bool data[N];

    public:
        bitvec2() {
            reset();
        }

        bitvec2<N> operator^(bitvec2<N> const& other) {
            bitvec2<N> ret;
            for (size_t i = 0; i < N; i++) {
                ret.data[i] = data[i] ^ other.data[i];
            }
            return ret;
        }

        bitvec2<N> operator|(bitvec2<N> const& other) {
            bitvec2<N> ret;
            for (size_t i = 0; i < N; i++) {
                ret.data[i] = data[i] | other.data[i];
            }
            return ret;
        }

        bool operator [] (int i) const {return data[i];}
        bool& operator [] (int i) {return data[i];}

        int count() {
            int tot = 0;
            for (size_t i = 0; i < N; i++) {
                tot += data[i] ? 1 : 0;
            }
            return tot;
        }

        void reset() {
            for (size_t i = 0; i < N; i++) {
                data[i] = false;
            }
        }

};

struct signature_t{
    bitvec2<phase_detector_constants::signature_len> bv;
    double mean_stride;
};

class PhaseDetector {
    private:
        signature_t current_signature;
        signature_t last_signature;

        // static hash<bitset<64>> hash_bitvec();
        //hash<uint64_t> hash_bitvec;

        uint64_t instruction_count = 0;
        uint64_t stable_count = 0;
        uint64_t last_ip = 0;
        double sum_delta = 0;

        phase_id_type phase = -1;

        vector<signature_t> phase_table;

        //phase trace?? should it be deque/stack or vector/arraylist?

        // vector<phase_id_type> phase_trace;
        deque<phase_id_type> phase_trace;

        vector<listener_function> listeners;
//        vector<listener_function_ariel> listeners_ariel;
    public:

        const uint64_t stable_min = 3; //modsim

        double difference_measure_of_signatures(signature_t sig1, signature_t sig2) {
            // auto xor_signatures = sig1 ^ sig2;
            // auto or_signatures = sig1 | sig2;
            return static_cast<double>((sig1.bv ^ sig2.bv).count()) / (sig1.bv | sig2.bv).count(); // this should work with any compiler
            // return ((double) xor_signatures.__builtin_count()) / or_signatures.__builtin_count(); // this might only work with GCC
        }

        uint64_t hash_address(uint64_t x) {
            // Drop low bits
            x = x >> phase_detector_constants::drop_bits;

            // Do the hash
            // Ref: https://stackoverflow.com/a/12996028
            x = (x ^ (x >> 30)) * UINT64_C(0xbf58476d1ce4e5b9);
            x = (x ^ (x >> 27)) * UINT64_C(0x94d049bb133111eb);
            x = x ^ (x >> 31);

            // Chop to only log2_sig_len bits
            x = x >> (64 - phase_detector_constants::log2_signature_len);
            return x;
        }

        #if 0
        uint64_t hash_address(uint64_t address) {

            return 0;
            return ((uint32_t) std::hash<bitset<64>>{}(address >> phase_detector_constants::drop_bits)) >> (32 /*sizeof(uint32_t)*/ /* likely 32 if 32-bit MT or 64 if 64-bit MT or other hash */ - phase_detector_constants::log2_signature_len);
        }
        #endif

        bool detect(uint64_t instruction_pointer, phase_id_type *new_phase) {
            current_signature.bv[hash_address(instruction_pointer)] = 1;
            //sum_delta += last_ip == 0 ? 0 : (((double)instruction_pointer-last_ip)) / phase_detector_constants::interval_len;
            //printf("Summing: %lf\n", (((double)instruction_pointer-last_ip)) / phase_detector_constants::interval_len);
            //printf("Delta: %" PRId64 "\n", (int64_t)instruction_pointer - (int64_t)last_ip);
            //last_ip = instruction_pointer;
            phase_id_type ret = increment_instruction_count();
            *new_phase = ret;
            if (ret != -2) { // -2 indicates no change, -1 is transition phase, 0.. is a real phase
                return true;
            }
            return false;
        }

        void init_phase_detector() {
            current_signature.bv.reset();
            last_signature.bv.reset();
            // hash_bitvec
            instruction_count = 0;
            stable_count = 0;
            phase = -1;
            phase_table.clear();
            phase_trace.clear();
            listeners.clear();
            sum_delta = 0;
            last_ip = 0;
        }

        void cleanup_phase_detector(string log_file_name) {
            if (log_file_name.size() > 0) {
                //print_log_file(log_file_name);
            }
            init_phase_detector();
        }
        void register_listeners(listener_function f) {
            listeners.push_back(f);
        }

#if 0
        void print_log_file(string log_file_name) {

            ofstream log(log_file_name);
            for (size_t index = 0; index < phase_trace.size(); index++) {
                auto p = phase_trace[index];
                if (log.is_open()) {
                    log << index /* * phase_detector_constants::interval_len */ << "," << p << '\n';
                }
            }
            log.close();
        }
#endif

        phase_id_type increment_instruction_count() {
            phase_id_type phase_ret = -2;
            if (instruction_count % phase_detector_constants::interval_len == 0) {
                    // we are on a boundary! determine phase and notify listeners
                    //first, check if the phase is stable since the difference measure is acceptably low
                    if (difference_measure_of_signatures(current_signature, last_signature) < phase_detector_constants::threshold) {
                        stable_count += 1;
                        if (stable_count >= stable_min && phase == -1) {
                            //add the current signature to the phase table and make the phase #/phase id to its index
                            phase_table.push_back(current_signature);
                            phase = phase_table.size() - 1; // or indexof curr_sig?
                            //line 194 in the python
                        }
                    } else { //line 196 in python
                        //if difference too high then it's not stable and we might now know the phase
                        stable_count = 0;
                        phase = -1;

                        //see if we've entered a phase we have seen before
                        if (!phase_table.empty()) { //line 201 python
                            double best_diff = phase_detector_constants::threshold;
                            for (auto phase_table_iterator = phase_table.begin(); phase_table_iterator != phase_table.end(); phase_table_iterator++) {
                                const auto s = *phase_table_iterator;
                                const auto diff = difference_measure_of_signatures(current_signature, s);
                                // difference_scores_from_phase_table.push_back(diff);
                                if (diff < phase_detector_constants::threshold && diff < best_diff) {
                                    phase = std::distance(phase_table.begin(), phase_table_iterator);
                                    best_diff = diff;
                                    //set current phase to the phase of the one with the lowest difference from current (which is the index in the phase table)
                                }
                            }
                        }
                    }
                    //whether or not the phase is stable, we need to update last phase and whatnot
                    last_signature = current_signature;
                    current_signature.bv.reset();
                    //printf("Sum delta: %lf\n", sum_delta);
                    sum_delta = 0;

                    //add the current phase ID to the phase trace - from line 209 in python
                    phase_trace.push_back(phase);

                    //notify listeners of the current phase ID
                    //from line 212 in python
                    for (auto f : listeners) {
                        f(phase);
                    }
                    phase_ret = phase;

            }
            instruction_count += 1;
            return phase_ret;
        }
};

#endif
