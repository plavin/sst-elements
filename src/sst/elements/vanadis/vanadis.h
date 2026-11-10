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

#ifndef _VANADIS_COMPONENT_H
#define _VANADIS_COMPONENT_H

#include "datastruct/cqueue.h"
#include "decoder/vdecoder.h"
#include "inst/isatable.h"
#include "inst/regfile.h"
#include "inst/regstack.h"
#include "inst/vinst.h"
#include "lsq/vlsq.h"
#include "lsq/vbasiclsq.h"
#include "velf/velfinfo.h"
#include "vfpflags.h"
#include "vfuncunit.h"
#include "vregreadycheck.h"
#include "vmemordercheck.h"
#include "vpipelinetrace.h"
#include "rocc/vroccinterface.h"
#include "rocc/vbasicrocc.h"

#include "os/vgetthreadstate.h"
#include "os/vdumpregsreq.h"
#include "os/vcheckpointreq.h"

#include <array>
#include <limits>
#include <set>
#include <sst/core/component.h>
#include <sst/core/interfaces/stdMem.h>
#include <sst/core/link.h>
#include <sst/core/output.h>
#include <sst/core/params.h>


namespace SST {
namespace Vanadis {

class VanadisInsCacheLoadRecord
{
public:
    VanadisInsCacheLoadRecord(const uint32_t thr, const uint64_t addrStart, const uint16_t len) :
        hw_thr(thr),
        addr(addrStart),
        width(len),
        hasPayload(false)
    {

        data = new uint8_t[len];

        for ( uint16_t i = 0; i < width; ++i ) {
            data[i] = 0;
        }
    }

    uint64_t getAddress() const { return addr; }
    uint32_t getHWThread() const { return hw_thr; }
    uint16_t getWidth() const { return width; }
    bool     hasData() const { return hasPayload; }
    uint8_t* getPayload() { return data; }

    void setPayload(uint8_t* ptr)
    {
        hasPayload = true;

        for ( uint16_t i = 0; i < width; ++i ) {
            data[i] = ptr[i];
        }
    }

private:
    bool           hasPayload;
    uint8_t*       data;
    const uint64_t addr;
    const uint16_t width;
    const uint32_t hw_thr;
};

class VanadisCore : public SST::Component
{

public:
    SST_ELI_REGISTER_COMPONENT(
        VanadisCore,
#ifdef VANADIS_BUILD_DEBUG
        "vanadisdbg",
#else
        "vanadis",
#endif
        "core",
        SST_ELI_ELEMENT_VERSION(1, 0, 0),
        "Vanadis Processor Component",
        COMPONENT_CATEGORY_PROCESSOR
    )

    // This is the old name, allow use so that existing config files don't break
    SST_ELI_REGISTER_ALIAS("dbg_VanadisCPU")

    SST_ELI_DOCUMENT_PARAMS(
        { "verbose", "Set the level of output verbosity, 0 is no output, higher "
                     "is more output", "0" },
        { "dbg_mask", "Mask for output. Default is to not mask anything out (0) and defer to 'verbose'.", "0"},
        { "start_verbose_when_issue_address", "Set verbose to 0 until the specified instruction "
                                        "address is issued, then set to 'verbose' parameter", ""},
        { "stop_verbose_when_retire_address", "When the specified instruction "
                                        "address is retired, set verbose to 0", ""},
        { "pause_when_retire_address", "If specified, the simulation will stop when this address is retired.", "0"},
        { "pipeline_trace_file", "If specified, a trace of the pipeline activity will be generated to this file.", ""},
        { "max_cycle", "Maximum number of cycles to execute. The core will halt after this many cycles." , "std::numeric_limits<uint64_t>::max()"},
        { "node_id", "Identifier for the node this core belongs to. Each node in the system needs a unique ID between 0 and (number of nodes) - 1. Used to tag output.", "0"},
        { "core_id", "Identifier for this core. Each core in the system needs a unique ID between 0 and (number of cores) - 1.", 0 },
        { "hardware_threads", "Number of hardware threads in this core", "1" },
        { "clock", "Core clock frequency", "1GHz" },
        { "reorder_slots", "Number of slots in the reorder buffer", "64"},
        { "physical_integer_registers", "Number of physical integer registers per hardware thread", "128" },
        { "physical_fp_registers", "Number of physical floating point registers per hardware thread", "128" },
        { "integer_arith_units", "Number of integer arithemetic units", "2" },
        { "integer_arith_cycles", "Cycles per instruction for integer arithmetic", "2" },
        { "integer_div_units", "Number of integer division units", "1" },
        { "integer_div_cycles", "Cycles per instruction for integer division", "4" },
        { "fp_arith_units", "Number of floating point arithmetic units", "2" },
        { "fp_arith_cycles", "Cycles per floating point arithmetic", "8" },
        { "fp_div_units", "Number of floating point division units", "1" },
        { "fp_div_cycles", "Cycles per floating point division", "80" },
        { "branch_units", "Number of branch units", "1" },
        { "branch_unit_cycles", "Cycles per branch", "int_arith_cycles"},
        { "issues_per_cycle", "Number of instruction issues per cycle", "2" },
        { "fetches_per_cycle", "Number of instruction fetches per cycle", "2" },
        { "retires_per_cycle", "Number of instruction retires per cycle", "2" },
        { "decodes_per_cycle", "Number of instruction decodes per cycle", "2" },
        { "dcache_line_width", "Width of a line for the data cache, in bytes. (Currently not used but may be in the future).", "64"},
        { "icache_line_width", "Width of a line for the instruction cache, in bytes", "64"},
        { "print_retire_tables", "Print registers during retirement step (default is yes)", "true" },
        { "print_issue_tables", "Print registers during issue step (default is yes)", "true" },
        { "print_int_reg", "Print integer registers true/false, auto set to true if verbose > 16", "false" },
        { "print_fp_reg", "Print floating-point registers true/false, auto set to "
                          "true if verbose > 16", "false" },
        { "print_rob", "Print reorder buffer state during issue and retire", "true"},
        { "enable_simt", "Implement SIMT pipeline for multithread kernels", "false"},
        { "reg_ready_at_writeback", "Default off. When true, a consumer instruction may issue as soon as the "
                                     "physical register(s) feeding its inputs have been written by the producer's execution "
                                     "(writeback), instead of waiting for the producer to fully retire. SYSCALL/FENCE/ROCC and "
                                     "LL/SC-or-AMO loads/stores keep the old retire-gated behavior for safety. Default false "
                                     "reproduces the original pendingWrites-at-retire semantics exactly.", "false"},
        { "reg_ready_check", "Debug-only, default off. When true, enables a read-only shadow checker "
                              "(see vregreadycheck.h) that fatal()s with instruction/phys-reg/cycle details on the "
                              "first provable use-after-free, read-before-write, or stale-writeback hazard on a "
                              "physical register. Purely observational: it never changes issue/execute/retire "
                              "control flow, so leaving it off (default) or on cannot change simulated cycle "
                              "counts.", "false"},
        { "mem_order_check", "Debug-only, default off. When true, enables a read-only, byte-granular "
                              "shadow-memory checker (see vmemordercheck.h) that tracks this core's own ordinary "
                              "store stream and fatal()s with full timing diagnostics on the first provable "
                              "memory-ordering violation (a load retiring with a value stale relative to program "
                              "order). Purely observational: it never changes issue/execute/retire control flow, "
                              "so leaving it off (default) or on cannot change simulated cycle counts.", "false"},
        { "stage_trace", "Debug-only, default off. When true (and stage_trace_count > 0), dumps a "
                          "STAGETRACE,... line per retiring instruction (within the stage_trace_skip/"
                          "stage_trace_count window) with the cycle it was first seen by issue, issued, "
                          "(loads/stores) sent to memory, (loads/stores) completed in memory, written back, "
                          "and retired (see vpipelinetrace.h). Purely observational bookkeeping plus a printf; "
                          "it never changes issue/execute/retire control flow, so leaving it off (default) or "
                          "on cannot change simulated cycle counts.", "false"},
        { "stage_trace_skip", "Debug-only. Number of retiring instructions to skip before stage_trace starts "
                               "dumping (to get past boot/startup).", "0"},
        { "stage_trace_count", "Debug-only. Number of retiring instructions to dump after stage_trace_skip. "
                                "0 (the default) means dump nothing even if stage_trace is on.", "0"},
        { "rename_in_order", "Default off. When true, physical registers are assigned to each instruction "
                              "by a separate rename stage that walks the ROB in strict program order, ahead of "
                              "issue, instead of renaming at issue time. Issue then only considers "
                              "already-renamed instructions and decides readiness purely from the "
                              "reg_ready_at_writeback physical-register ready bits, skipping the ISA-indexed "
                              "WAR/WAW tmp-table checks. Requires reg_ready_at_writeback=true (fatal otherwise). "
                              "SYSCALL/RoCC instructions are never renamed ahead of issue; they and everything "
                              "behind them in the ROB fall back to the old combined check-and-rename-at-issue "
                              "path.", "false"},
        { "rename_width", "Default off feature (see rename_in_order). Maximum number of not-yet-renamed "
                           "instructions the rename stage will rename per hardware thread per cycle. Defaults to "
                           "decodes_per_cycle.", "decodes_per_cycle"}  )

    SST_ELI_DOCUMENT_STATISTICS(
        { "cycles", "Number of cycles the core executed", "cycles", 1 },
        { "syscall-cycles",
          "Number of cycles spent waiting on execution of SYSCALL in OS "
          "components",
          "cycles", 1 },
        { "rob_slots_in_use", "Number of micro-ops in the ROB each cycle", "instructions", 1 },
        { "rob_cleared_entries", "Number of micro-ops that are cleared during a pipeline clear", "instructions", 1 },
        { "instructions_issued", "Number of instructions issued", "instructions", 1 },
        { "instructions_retired", "Number of instructions retired", "instructions", 1 },
        { "instructions_decoded", "Number of instructions decoded", "instructions", 1 },
        { "branch_mispredicts", "Number of retired branches which were mis-predicted", "instructions", 1 },
        { "branches", "Number of retired branches", "instructions", 1 },
        { "loads_issued", "Number of load instructions issued to the LSQ", "instructions", 1 },
        { "stores_issued", "Number of store instructions issued to the LSQ", "instructions", 1 },
        { "phys_int_reg_in_use", "Number of physical integer registers that are in use each cycle", "registers", 1 },
        { "phys_fp_reg_in_use", "Number of physical floating point registers than are in use each cycle", "registers",
          1 },
        { "mem_block_unalloc_seen", "LOAD/STORE/FENCE skipped allocation because an earlier mem-op this cycle already failed to allocate", "instructions", 1 },
        { "mem_block_out_reg_avail", "LOAD/STORE/FENCE blocked at issue: no free physical register for output", "instructions", 1 },
        { "mem_block_in_raw", "LOAD/STORE/FENCE blocked at issue: an input operand (e.g. address register) still has a pending write", "instructions", 1 },
        { "mem_block_out_hazard", "LOAD/STORE/FENCE blocked at issue: WAW/WAR vs an earlier not-yet-issued instruction", "instructions", 1 },
        { "mem_block_lsq_full", "LOAD/STORE blocked at issue: resources ok but LSQ op queue is full", "instructions", 1 },
        { "wbready_int_raw_block", "Issue blocked on an integer input whose producer has not yet written back (reg_ready_at_writeback path)", "instructions", 1 },
        { "wbready_fp_raw_block", "Issue blocked on an FP input whose producer has not yet written back (reg_ready_at_writeback path)", "instructions", 1 },
        { "rename_stall_no_regs", "rename_in_order path: the in-order rename stage stopped (for a cycle, a thread) because the oldest not-yet-renamed instruction had no free physical register(s) available", "instructions", 1 },
        { "renamed_not_issued", "rename_in_order path: number of instructions that have been renamed but not yet issued, sampled each cycle", "instructions", 1 })

    SST_ELI_DOCUMENT_PORTS({ "icache_link", "Connects the CPU to the instruction cache", {} },
                           { "dcache_link", "Connects the CPU to the data cache", {} },
                           { "os_link", "Connects this handler to the main operating system of the node", {}} )

    // Optional since there is nothing to document
    SST_ELI_DOCUMENT_SUBCOMPONENT_SLOTS(
        { "lsq", "Load-Store Queue for Memory Access", "SST::Vanadis::VanadisLoadStoreQueue" },
        { "decoder", "Decoder to use", "SST::Vanadis::VanadisDecoder" },
        { "rocc", "RoCC accelerator interface(s), optional", "SST::Vanadis::VanadisRoCCInterface" },
        { "mem_interface_inst", "Interface to memory system for instructions", "SST::Interfaces::StandardMem" },
    )

    VanadisCore(SST::ComponentId_t id, SST::Params& params);
    ~VanadisCore();

    void init(unsigned int phase) override;

    void setup() override;
    void finish() override;

    void printStatus(SST::Output& output) override;

    //    void handleIncomingDataCacheEvent( StandardMem::Request* ev );
    void handleIncomingInstCacheEvent(StandardMem::Request* ev);
    void recvOSEvent(SST::Event* ev);

    void handleMisspeculate(const uint32_t hw_thr, const uint64_t new_ip);
    void clearROBMisspeculate(const uint32_t hw_thr);

    void clearFuncUnit(const uint32_t hw_thr, std::vector<VanadisFunctionalUnit*>& unit);

    void syscallReturn(uint32_t thr);
    void setHalt(uint32_t thr, int64_t halt_code);
    void startThread(int thr, uint64_t stackStart, uint64_t instructionPointer );
    void startThreadFork( VanadisStartThreadForkReq* req );
    void startThreadClone( VanadisStartThreadCloneReq* req );
    void startThreadClone3( VanadisStartThreadClone3Req* req );
    void getThreadState( VanadisGetThreadStateReq* req );
    void dumpRegs( VanadisDumpRegsReq* req );

private:
    VanadisCore();                        // for serialization only
    VanadisCore(const VanadisCore&); // do not implement
    void operator=(const VanadisCore&);   // do not implement

    virtual bool tick(SST::Cycle_t);

    void resetRegisterUseTemps(const int hw_thr, const uint16_t i_reg, const uint16_t f_reg);

    int assignRegistersToInstruction(
        const uint16_t int_reg_count, const uint16_t fp_reg_count, VanadisInstruction* ins,
        VanadisRegisterStack* int_regs, VanadisRegisterStack* fp_regs, VanadisISATable* isa_table);

    int checkInstructionResources(
        VanadisInstruction* ins, VanadisRegisterStack* int_regs, VanadisRegisterStack* fp_regs,
        VanadisISATable* isa_table, bool already_renamed = false);

    int recoverRetiredRegisters(
        VanadisInstruction* ins, VanadisRegisterStack* int_regs, VanadisRegisterStack* fp_regs,
        VanadisISATable* issue_isa_table, VanadisISATable* retire_isa_table);

    int recoverRetiredRegisters(
        VanadisInstruction* ins, VanadisRegisterStack* int_regs, VanadisRegisterStack* fp_regs,
        VanadisISATable* issue_isa_table, VanadisISATable* retire_isa_table, uint16_t sw_thr);

    void performFetch(const uint64_t cycle);
    void performDecode(const uint64_t cycle);
    void performRename(const uint64_t cycle, int hwThr);
    int  performIssue(const uint64_t cycle, int hwThr, uint32_t& rob_start, int& unallocated_memory_op_seen);
    void performExecute(const uint64_t cycle);
    int  performRetire(int rob_num, VanadisCircularQueue<VanadisInstruction*>* rob, const uint64_t cycle);
    int  allocateFunctionalUnit(VanadisInstruction* ins);
    bool mapInstructiontoFunctionalUnit(VanadisInstruction* ins, std::vector<VanadisFunctionalUnit*>& functional_units);
    void printRob(int rob_num, VanadisCircularQueue<VanadisInstruction*>* rob);

    bool checkVerboseAddr( uint64_t addr ) {
        for ( auto& it : start_verbose_when_issue_address ) {
            if ( it == addr ) return true;
        }
        return false;
    }

    void setVerboseWhenIssueAddress( std::string addrs ) {
        while ( ! addrs.empty() ) {
            auto pos = addrs.find(',');
            std::string addr;
            if ( pos == std::string::npos ) {
                addr = addrs;
                addrs.clear();
            } else  {
                addr = addrs.substr(0,pos);
                addrs = addrs.substr(pos+1);
            }
            start_verbose_when_issue_address.push_back(  strtol( addr.c_str(), NULL , 16 ) );
        }
    }

    void resetHwThread(uint32_t thr);

    SST::Output* output = nullptr;

    uint16_t core_id;
    uint64_t current_cycle;
    uint64_t max_cycle;
    uint32_t hw_threads;

    uint32_t fetches_per_cycle;
    uint32_t decodes_per_cycle;
    uint32_t issues_per_cycle;
    uint32_t retires_per_cycle;

    uint32_t m_curRetireHwThread;
    uint32_t m_curIssueHwThread;

    std::vector<VanadisCircularQueue<VanadisInstruction*>*> rob;
    std::vector<VanadisCircularQueue<VanadisInstruction*>*> v_warp_rob;
    std::vector<VanadisDecoder*>                            thread_decoders;
    std::vector<const VanadisDecoderOptions*>               isa_options;

    std::vector<VanadisFunctionalUnit*> fu_int_arith;
    std::vector<VanadisFunctionalUnit*> fu_int_div;
    std::vector<VanadisFunctionalUnit*> fu_branch;
    std::vector<VanadisFunctionalUnit*> fu_fp_arith;
    std::vector<VanadisFunctionalUnit*> fu_fp_div;

    std::vector<VanadisRegisterFile*>  register_files;
    VanadisRegisterStack* int_register_stack = nullptr;
    VanadisRegisterStack* fp_register_stack = nullptr;

    // Register-ready-at-writeback dependency tracking (default off, see
    // "reg_ready_at_writeback" param). Indexed by *physical* register id (shared pool across
    // hw-threads, same indexing as int_register_stack/fp_register_stack). int_reg_ready_/
    // fp_reg_ready_[p]==1 means the value currently held in physical reg p is valid to read
    // without waiting for its producer to retire. int_reg_needs_retire_/fp_reg_needs_retire_[p]==1
    // forces consumers to fall back to the original pendingWrites-at-retire check for that phys
    // reg regardless of the ready bit (used for SYSCALL/FENCE/ROCC outputs and LL/SC loads/stores,
    // which keep the old semantics). Both vectors are no-ops (never consulted) when the param is
    // off, and are maintained unconditionally cheaply so there is no divergent code path risk.
    bool reg_ready_at_writeback = false;
    std::vector<uint8_t> int_reg_ready_;
    std::vector<uint8_t> fp_reg_ready_;
    std::vector<uint8_t> int_reg_needs_retire_;
    std::vector<uint8_t> fp_reg_needs_retire_;

    // rename_in_order only: per-physical-register "has this phys reg's producer retired" bit,
    // maintained alongside reg_ready_at_writeback's other per-phys-reg vectors. Needed because
    // the ISA-indexed pendingIntWrites/pendingFPWrites counters that int_reg_needs_retire_'s
    // fallback normally uses assume rename and the readiness check happen in the same call; under
    // rename_in_order they can count multiple in-flight renamed-but-unretired writers of the same
    // ISA register and so never reach zero. Only consulted from checkInstructionResources's
    // already_renamed branch; a no-op when rename_in_order is off.
    std::vector<uint8_t> int_reg_retired_;
    std::vector<uint8_t> fp_reg_retired_;

    void markPhysRegsReady(VanadisInstruction* ins);
    static bool wbreadyRequiresRetireGating(VanadisInstruction* ins);

    // In-order rename stage (default off, see "rename_in_order" param). Requires
    // reg_ready_at_writeback. Each cycle, before issue, performRename walks each thread's ROB
    // from the oldest instruction, renaming up to rename_width not-yet-renamed instructions,
    // stopping at the first one with no free physical register or at a SYSCALL/RoCC instruction
    // (left, along with everything behind it, for the old combined check+rename-at-issue path
    // once it reaches the ROB front).
    bool rename_in_order = false;
    uint32_t rename_width = 2;
    Statistic<uint64_t>* stat_rename_stall_no_regs = nullptr;
    Statistic<uint64_t>* stat_renamed_not_issued = nullptr;

    // Apply the mem_order_check shadow checker's retire-time hooks for a genuinely
    // retiring (not squashed) instruction -- commits a STORE's captured payload into the shadow,
    // checks a LOAD's captured response bytes against the shadow, and clears the whole shadow on
    // a SYSCALL (the OS can touch memory this core's own store stream never observed). No-op
    // unless mem_order_check is on. Always calls mem_order_checker_.forget(ins) afterwards.
    void applyMemOrderCheckAtRetire(VanadisInstruction* ins);

    // Debug-only, gated by reg_ready_check. Extends the
    // shadow checker to catch a class of bug it otherwise cannot see: a consumer that captures a
    // STALE (too-old) ISA->phys mapping -- e.g. because an issue-gating relaxation let it issue
    // before a newer, not-yet-renamed writer of the same ISA register got a chance to rename --
    // would still pass checkRead/checkWrite cleanly (the stale producer legitimately wrote that
    // phys reg), but is architecturally wrong. Scans the ROB from the front to find the nearest
    // STRICTLY OLDER, already-issued writer of isa_reg before reaching ins itself; if one exists,
    // asserts captured_phys_reg equals that writer's own output phys reg. If no in-ROB writer is
    // found at all, falls back to comparing against the retired mapping (retire_isa_tables), which
    // must then be the true source of truth. Called right after each ISA->phys input lookup in
    // assignRegistersToInstruction. No-op unless reg_ready_check is on.
    void debugCheckRenameConsistency(
        const int hw_thr, VanadisInstruction* ins, uint16_t isa_reg, uint16_t captured_phys_reg, bool is_int);

    // Shadow checker state (debug-only, "reg_ready_check" param, default off; see
    // vregreadycheck.h for the full design writeup). Indexed by *physical* register id, same
    // pool/indexing as int_reg_ready_/fp_reg_ready_ above. All maintained unconditionally
    // (cheap) but only ever consulted/fatal()-capable when reg_ready_check is true.
    bool reg_ready_check = false;
    std::vector<uint32_t> int_reg_gen_;
    std::vector<uint32_t> fp_reg_gen_;
    std::vector<uint8_t>  int_reg_written_;
    std::vector<uint8_t>  fp_reg_written_;
    std::vector<uint64_t> int_reg_producer_;
    std::vector<uint64_t> fp_reg_producer_;
    VanadisRegReadyChecker::InsGenMap chk_int_in_gen_;
    VanadisRegReadyChecker::InsGenMap chk_fp_in_gen_;
    VanadisRegReadyChecker::InsGenMap chk_int_out_gen_;
    VanadisRegReadyChecker::InsGenMap chk_fp_out_gen_;
    VanadisRegReadyChecker reg_ready_checker_;

    // Byte-granular shadow memory-order checker state (debug-only, "mem_order_check"
    // param, default off; see vmemordercheck.h for the full design writeup). Entirely no-op
    // (reads/writes nothing, cannot affect cycle counts) unless mem_order_check is true.
    bool mem_order_check = false;
    VanadisMemOrderChecker mem_order_checker_;

    // Debug-only per-instruction stage-timestamp dumper ("stage_trace" param, default off; see
    // vpipelinetrace.h). Entirely no-op (not even a map lookup) unless stage_trace is true.
    bool stage_trace = false;
    VanadisPipelineTracer pipeline_tracer_;

    std::vector<VanadisISATable*> issue_isa_tables;
    std::vector<VanadisISATable*> retire_isa_tables;

    std::vector<uint8_t*> tmp_not_issued_int_reg_read;
    std::vector<uint8_t*> tmp_int_reg_write;
    std::vector<uint8_t*> tmp_not_issued_fp_reg_read;
    std::vector<uint8_t*> tmp_fp_reg_write;

    std::list<VanadisInsCacheLoadRecord*>* icache_load_records = nullptr;

    VanadisLoadStoreQueue* lsq = nullptr;
    StandardMem*           memInstInterface = nullptr;

    std::vector<VanadisRoCCInterface*> roccs_;
    std::vector<std::deque<VanadisInstruction*>> rocc_queues_;

    uint32_t decode_start_thread_ = 0;

    bool* halted_masks = nullptr;
    bool  print_int_reg;
    bool  print_fp_reg;
    bool  print_issue_tables;
    bool  print_retire_tables;
    bool  print_rob;
    bool enable_simt; //for future use

    // Tracing, debug, etc.
    #ifdef VANADIS_BUILD_DEBUG
    char*    inst_print_buffer_ = nullptr;
    #endif
    FILE*    pipeline_trace_file_ = nullptr;

    uint64_t dCacheLineWidth;
    uint64_t iCacheLineWidth;

    TimeConverter           clock_tc_;
    Clock::HandlerBase*     clock_handler_ = nullptr;

    Statistic<uint64_t>* stat_ins_retired = nullptr;
    Statistic<uint64_t>* stat_ins_decoded = nullptr;
    Statistic<uint64_t>* stat_ins_issued = nullptr;
    Statistic<uint64_t>* stat_loads_issued = nullptr;
    Statistic<uint64_t>* stat_stores_issued = nullptr;
    Statistic<uint64_t>* stat_branch_mispredicts = nullptr;
    Statistic<uint64_t>* stat_branches = nullptr;
    Statistic<uint64_t>* stat_cycles = nullptr;
    Statistic<uint64_t>* stat_rob_entries = nullptr;
    Statistic<uint64_t>* stat_rob_cleared_entries = nullptr;
    Statistic<uint64_t>* stat_syscall_cycles = nullptr;
    Statistic<uint64_t>* stat_int_phys_regs_in_use = nullptr;
    Statistic<uint64_t>* stat_fp_phys_regs_in_use = nullptr;

    // Additive instrumentation counting reasons a LOAD/STORE fails to allocate into the LSQ at
    // issue time.
    Statistic<uint64_t>* stat_mem_block_unalloc_seen = nullptr;   // skipped: earlier mem op this cycle already failed to allocate
    Statistic<uint64_t>* stat_mem_block_out_reg_avail = nullptr;  // checkInstructionResources code 1: no free phys reg
    Statistic<uint64_t>* stat_mem_block_in_raw = nullptr;         // code 2/3: input operand (e.g. address reg) write still pending
    Statistic<uint64_t>* stat_mem_block_out_hazard = nullptr;     // code 4/5: WAW/WAR vs an earlier not-yet-issued op
    Statistic<uint64_t>* stat_mem_block_lsq_full = nullptr;       // resource_check passed but lsq->loadFull()/storeFull()

    // Counts of RAW blocks that occurred while checking the reg_ready_at_writeback ready-bit
    // path (i.e. would-be-blocked-on-input-not-yet-written-back), for ANY instruction (not just
    // mem ops like the counters above). Zero when the param is off.
    Statistic<uint64_t>* stat_wbready_int_raw_block = nullptr;
    Statistic<uint64_t>* stat_wbready_fp_raw_block = nullptr;

    uint32_t ins_issued_this_cycle;
    uint32_t ins_retired_this_cycle;
    uint32_t ins_decoded_this_cycle;

    uint64_t pause_on_retire_address;
    std::deque<uint64_t> start_verbose_when_issue_address;
    uint64_t stop_verbose_when_retire_address;

    std::vector<VanadisFloatingPointFlags*> fp_flags;
    SST::Link* os_link = nullptr;

    bool* m_checkpointing = nullptr;
    std::string m_checkpointDir;
    enum { NO_CHECKPOINT, CHECKPOINT_LOAD, CHECKPOINT_SAVE } m_checkpoint;
    void checkpoint(FILE*);
    void checkpointLoad(FILE*);
};

} // namespace Vanadis
} // namespace SST

#endif /* _VANADIS_COMPONENT_H */
