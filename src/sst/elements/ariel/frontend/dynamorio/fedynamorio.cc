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

#include "dr_api.h"
#include "dr_ir_decode.h"
#include "drmgr.h"
#include "drreg.h"
#include "drutil.h"
#include "dr_os_utils.h"

#include <sst/core/interprocess/shmchild.h>
#include "ariel_shmem.h"

#include <stdlib.h>
#include <string.h>

using namespace SST::ArielComponent;

namespace {

class ArielTunnelDR : public SST::Core::Interprocess::SHMChild<ArielTunnel> {
public:
    explicit ArielTunnelDR(const std::string& region_name) :
        SST::Core::Interprocess::SHMChild<ArielTunnel>(region_name)
    {}
};

// Maximum number of memory writes per instruction whose payload capture can be
// deferred until after the instruction executes.
#define ARIEL_DR_MAX_PENDING_WRITES 8

struct pending_write_t {
    uint64_t addr;
    uint32_t size;
};

struct thread_state_t {
    uint32_t        core_idx;
    bool            emit;
    uint32_t        pending_count;
    pending_write_t pending[ARIEL_DR_MAX_PENDING_WRITES];
};

// How an application instruction is reported to Ariel
enum inst_kind_t {
    INST_KIND_PLAIN,      // no memory, not FP: reported as ARIEL_NOOP
    INST_KIND_FP_COMPUTE, // no memory, FP: START/END (for FP stats) followed by ARIEL_NOOP
    INST_KIND_MEMORY      // START, READ/WRITE refs, END
};

enum memref_mode_t {
    MEMREF_READ,          // emit ARIEL_PERFORM_READ immediately
    MEMREF_WRITE,         // emit ARIEL_PERFORM_WRITE immediately
    MEMREF_PENDING_WRITE, // record write, emit after the instruction executes
    MEMREF_SINGLE         // instruction has exactly one ref: emit START/ref/END in one call
};

static int         tls_idx;
static void*       mapping_lock;
static uint32_t    max_cores          = 1;
static uint32_t    next_core_id       = 0;
static uint32_t    dropped_threads    = 0;
static bool        write_payload      = false;
static ArielTunnelDR* tunnel_manager  = NULL;
static ArielTunnel*   tunnel          = NULL;

static dr_emit_flags_t event_app_instruction(void* drcontext, void* tag, instrlist_t* bb,
    instr_t* where, bool for_trace, bool translating, void* user_data);

static bool
get_or_assign_core(uint32_t* assigned)
{
    bool ok = true;
    dr_mutex_lock(mapping_lock);
    if ( next_core_id >= max_cores ) {
        dropped_threads++;
        ok = false;
    }
    else {
        *assigned = next_core_id;
        next_core_id++;
    }
    dr_mutex_unlock(mapping_lock);
    return ok;
}

static inline thread_state_t*
get_thread_state(void* drcontext)
{
    return (thread_state_t*)drmgr_get_tls_field(drcontext, tls_idx);
}

static inline thread_state_t*
get_emitting_thread_state()
{
    thread_state_t* st = get_thread_state(dr_get_current_drcontext());
    if ( st == NULL || !st->emit ) return NULL;
    return st;
}

#if defined(AARCH64)
// Map an FP element/register width in bytes to an Ariel instruction class.
// Ariel only distinguishes single and double precision, so half precision is
// reported as single precision.
static uint32_t
fp_class_for_width(uint32_t bytes)
{
    if ( bytes == 8 ) return ARIEL_INST_DP_FP;
    if ( bytes == 4 || bytes == 2 ) return ARIEL_INST_SP_FP;
    return ARIEL_INST_UNKNOWN;
}

// Width in bytes of the widest FP/SIMD register operand of the instruction
static uint32_t
max_simd_reg_bytes(instr_t* ins)
{
    uint32_t max_bytes = 0;
    for ( int i = 0; i < instr_num_dsts(ins) + instr_num_srcs(ins); ++i ) {
        opnd_t op = (i < instr_num_dsts(ins)) ? instr_get_dst(ins, i) : instr_get_src(ins, i - instr_num_dsts(ins));
        if ( !opnd_is_reg(op) || !reg_is_simd(opnd_get_reg(op)) ) continue;
        reg_id_t reg   = opnd_get_reg(op);
        uint32_t bytes = reg_is_z(reg) ? proc_get_vector_length_bytes() : opnd_size_in_bytes(reg_get_size(reg));
        if ( bytes > max_bytes ) max_bytes = bytes;
    }
    return max_bytes;
}

// AArch64: DynamoRIO marks scalar FP instructions with DR_INSTR_CATEGORY_FP and
// encodes precision in the register width (h/s/d). Advanced SIMD FP instructions
// are only categorized as SIMD; their element width is carried as the trailing
// immediate operand (1 = half, 2 = single, 3 = double) and all of their
// mnemonics start with 'f'. SVE operands carry an explicit element size.
static void
classify_instruction(instr_t* ins, uint32_t* inst_class, uint32_t* simd_elem_count)
{
    *inst_class      = ARIEL_INST_UNKNOWN;
    *simd_elem_count = 1;

    const char* opname = decode_opcode_name(instr_get_opcode(ins));
    if ( opname == NULL ) return;
    if ( strncmp(opname, "fmov", 4) == 0 ) return; // moves are not FP operations

    uint category = instr_get_category(ins);
    if ( (category & (DR_INSTR_CATEGORY_LOAD | DR_INSTR_CATEGORY_STORE)) != 0 ) return;

    if ( (category & DR_INSTR_CATEGORY_FP) != 0 ) {
        // Scalar FP: precision from the destination (or first) FP register
        uint32_t bytes = 0;
        for ( int i = 0; i < instr_num_dsts(ins) + instr_num_srcs(ins) && bytes == 0; ++i ) {
            opnd_t op = (i < instr_num_dsts(ins)) ? instr_get_dst(ins, i) : instr_get_src(ins, i - instr_num_dsts(ins));
            if ( opnd_is_reg(op) && reg_is_simd(opnd_get_reg(op)) ) bytes = opnd_size_in_bytes(reg_get_size(opnd_get_reg(op)));
        }
        *inst_class = fp_class_for_width(bytes);
        return;
    }

    if ( (category & DR_INSTR_CATEGORY_SIMD) == 0 || opname[0] != 'f' ) return;

    // Vector FP: find the element width
    uint32_t elem_bytes = 0;
    for ( int i = 0; i < instr_num_dsts(ins) && elem_bytes == 0; ++i ) {
        opnd_t op = instr_get_dst(ins, i);
        if ( opnd_is_reg(op) && opnd_get_vector_element_size(op) != OPSZ_NA ) {
            elem_bytes = opnd_size_in_bytes(opnd_get_vector_element_size(op));
        }
    }
    if ( elem_bytes == 0 ) {
        for ( int i = instr_num_srcs(ins) - 1; i >= 0; --i ) {
            opnd_t op = instr_get_src(ins, i);
            if ( opnd_is_immed_int(op) ) {
                ptr_int_t elsz = opnd_get_immed_int(op);
                if ( elsz >= 1 && elsz <= 3 ) elem_bytes = 1u << elsz;
                break;
            }
        }
    }

    uint32_t reg_bytes = max_simd_reg_bytes(ins);
    *inst_class        = fp_class_for_width(elem_bytes);
    if ( *inst_class != ARIEL_INST_UNKNOWN && reg_bytes > elem_bytes ) {
        *simd_elem_count = reg_bytes / elem_bytes;
    }
}
#else
// x86: mirror the Pin frontend's mnemonic suffix heuristic (fesimple.cc)
static void
classify_instruction(instr_t* ins, uint32_t* inst_class, uint32_t* simd_elem_count)
{
    *inst_class      = ARIEL_INST_UNKNOWN;
    *simd_elem_count = 1;

    const char* opname = decode_opcode_name(instr_get_opcode(ins));
    if ( opname == NULL ) return;

    size_t len = strlen(opname);
    if ( len < 2 ) return;
    if ( strncmp(opname, "mov", 3) == 0 ) return;

    uint32_t max_simd_width = 1;
    for ( int i = 0; i < instr_num_dsts(ins) + instr_num_srcs(ins); ++i ) {
        opnd_t op = (i < instr_num_dsts(ins)) ? instr_get_dst(ins, i) : instr_get_src(ins, i - instr_num_dsts(ins));
        if ( !opnd_is_reg(op) ) continue;
        reg_id_t reg = opnd_get_reg(op);
        if ( reg_is_strictly_zmm(reg) ) max_simd_width = 8;
        else if ( reg_is_strictly_ymm(reg) && max_simd_width < 4 ) max_simd_width = 4;
        else if ( reg_is_strictly_xmm(reg) && max_simd_width < 2 ) max_simd_width = 2;
    }

    const char* suffix = opname + (len - 2);
    if ( strcmp(suffix, "pd") == 0 ) {
        *inst_class      = ARIEL_INST_DP_FP;
        *simd_elem_count = max_simd_width;
    }
    else if ( strcmp(suffix, "ps") == 0 ) {
        *inst_class      = ARIEL_INST_SP_FP;
        *simd_elem_count = max_simd_width * 2;
    }
    else if ( strcmp(suffix, "sd") == 0 ) {
        *inst_class = ARIEL_INST_DP_FP;
    }
    else if ( strcmp(suffix, "ss") == 0 ) {
        *inst_class = ARIEL_INST_SP_FP;
    }
}
#endif

static inline bool
instr_touches_memory(instr_t* ins)
{
    return instr_reads_memory(ins) || instr_writes_memory(ins);
}

static inst_kind_t
get_inst_kind(instr_t* ins)
{
    if ( instr_touches_memory(ins) ) return INST_KIND_MEMORY;
    uint32_t inst_class, simd_elem_count;
    classify_instruction(ins, &inst_class, &simd_elem_count);
    return (inst_class == ARIEL_INST_UNKNOWN) ? INST_KIND_PLAIN : INST_KIND_FP_COMPUTE;
}

static void
emit_instruction_marker(uint32_t core_idx, ArielShmemCmd_t marker, app_pc pc,
    uint32_t inst_class, uint32_t simd_elem_count)
{
    ArielCommand ac;
    memset(&ac, 0, sizeof(ac));
    ac.command = marker;
    ac.instPtr = (uint64_t)pc;
    ac.inst.instClass = inst_class;
    ac.inst.simdElemCount = simd_elem_count;
    tunnel->writeMessage(core_idx, ac);
}

static void
emit_noops(uint32_t core_idx, app_pc pc, uint32_t count)
{
    ArielCommand ac;
    memset(&ac, 0, sizeof(ac));
    ac.command = ARIEL_NOOP;
    ac.instPtr = (uint64_t)pc;
    for ( uint32_t i = 0; i < count; ++i ) {
        tunnel->writeMessage(core_idx, ac);
    }
}

static void
emit_memory_ref(uint32_t core_idx, app_pc pc, app_pc addr, uint32_t size, bool is_write,
    uint32_t inst_class, uint32_t simd_elem_count)
{
    ArielCommand ac;
    memset(&ac, 0, sizeof(ac));
    ac.command = is_write ? ARIEL_PERFORM_WRITE : ARIEL_PERFORM_READ;
    ac.instPtr = (uint64_t)pc;
    ac.inst.addr = (uint64_t)addr;
    ac.inst.size = size;
    ac.inst.instClass = inst_class;
    ac.inst.simdElemCount = simd_elem_count;
    if ( is_write && write_payload ) {
        // Only the copied bytes are limited; the reported access size is unchanged (as in Pin)
        size_t copy_size  = (size > ARIEL_MAX_PAYLOAD_SIZE) ? ARIEL_MAX_PAYLOAD_SIZE : size;
        size_t bytes_read = 0;
        dr_safe_read((const void*)addr, copy_size, &ac.inst.payload[0], &bytes_read);
    }
    tunnel->writeMessage(core_idx, ac);
}

static void
event_exit(void)
{
    if ( dropped_threads > 0 ) {
        dr_printf("SSTARIEL-DR: dropped %u thread(s) due to core-count limit\n", dropped_threads);
    }

    if ( tunnel != NULL ) {
        ArielCommand ac;
        memset(&ac, 0, sizeof(ac));
        ac.command = ARIEL_PERFORM_EXIT;
        ac.instPtr = 0;
        tunnel->writeMessage(0, ac);
    }

    if ( mapping_lock != NULL ) {
        dr_mutex_destroy(mapping_lock);
        mapping_lock = NULL;
    }

    if ( tls_idx >= 0 ) {
        drmgr_unregister_tls_field(tls_idx);
        tls_idx = -1;
    }

    drmgr_unregister_bb_insertion_event(event_app_instruction);
    drutil_exit();
    drreg_exit();
    drmgr_exit();

    delete tunnel_manager;
    tunnel_manager = NULL;
    tunnel = NULL;
}

static void
event_thread_init(void* drcontext)
{
    thread_state_t* st = (thread_state_t*)dr_thread_alloc(drcontext, sizeof(thread_state_t));
    memset(st, 0, sizeof(thread_state_t));

    uint32_t assigned_core = 0;
    if ( get_or_assign_core(&assigned_core) ) {
        st->core_idx = assigned_core;
        st->emit     = true;
    }
    else {
        dr_printf("SSTARIEL-DR: dropping thread %d (configured cores=%u, dropped=%u)\n",
            dr_get_thread_id(drcontext), max_cores, dropped_threads);
    }

    drmgr_set_tls_field(drcontext, tls_idx, st);

    if ( !st->emit ) return;

    ArielCommand ac;
    memset(&ac, 0, sizeof(ac));
    ac.command = ARIEL_NOOP;
    ac.instPtr = 0;
    tunnel->writeMessage(st->core_idx, ac);
}

static void
event_thread_exit(void* drcontext)
{
    thread_state_t* st = (thread_state_t*)drmgr_get_tls_field(drcontext, tls_idx);
    dr_thread_free(drcontext, st, sizeof(thread_state_t));
}

static void
record_instruction_start(app_pc pc, uint32_t inst_class, uint32_t simd_elem_count)
{
    thread_state_t* st = get_emitting_thread_state();
    if ( st == NULL ) return;
    emit_instruction_marker(st->core_idx, ARIEL_START_INSTRUCTION, pc, inst_class, simd_elem_count);
}

static void
record_instruction_end(app_pc pc)
{
    thread_state_t* st = get_emitting_thread_state();
    if ( st == NULL ) return;
    emit_instruction_marker(st->core_idx, ARIEL_END_INSTRUCTION, pc, ARIEL_INST_UNKNOWN, 1);
}

static void
record_noops(app_pc pc, uint32_t count)
{
    thread_state_t* st = get_emitting_thread_state();
    if ( st == NULL ) return;
    emit_noops(st->core_idx, pc, count);
}

// FP instruction without memory operands: an empty START/END pair carries the FP
// class into Ariel's FP statistics and the NOOP accounts for the instruction itself.
static void
record_fp_compute(app_pc pc, uint32_t inst_class, uint32_t simd_elem_count)
{
    thread_state_t* st = get_emitting_thread_state();
    if ( st == NULL ) return;
    emit_instruction_marker(st->core_idx, ARIEL_START_INSTRUCTION, pc, inst_class, simd_elem_count);
    emit_instruction_marker(st->core_idx, ARIEL_END_INSTRUCTION, pc, ARIEL_INST_UNKNOWN, 1);
    emit_noops(st->core_idx, pc, 1);
}

static void
record_memory_read(app_pc pc, app_pc addr, uint32_t size, uint32_t inst_class, uint32_t simd_elem_count)
{
    thread_state_t* st = get_emitting_thread_state();
    if ( st == NULL ) return;
    emit_memory_ref(st->core_idx, pc, addr, size, false, inst_class, simd_elem_count);
}

static void
record_memory_write(app_pc pc, app_pc addr, uint32_t size, uint32_t inst_class, uint32_t simd_elem_count)
{
    thread_state_t* st = get_emitting_thread_state();
    if ( st == NULL ) return;
    emit_memory_ref(st->core_idx, pc, addr, size, true, inst_class, simd_elem_count);
}

// Fast path for the common single-reference instruction: START, ref, END in one clean call
static void
record_single_ref(app_pc pc, app_pc addr, uint32_t size, uint32_t is_write, uint32_t inst_class,
    uint32_t simd_elem_count)
{
    thread_state_t* st = get_emitting_thread_state();
    if ( st == NULL ) return;
    emit_instruction_marker(st->core_idx, ARIEL_START_INSTRUCTION, pc, inst_class, simd_elem_count);
    emit_memory_ref(st->core_idx, pc, addr, size, is_write != 0, inst_class, simd_elem_count);
    emit_instruction_marker(st->core_idx, ARIEL_END_INSTRUCTION, pc, ARIEL_INST_UNKNOWN, 1);
}

// Payload tracing: remember the write so its data can be read after the store executes
static void
record_pending_write(app_pc pc, app_pc addr, uint32_t size, uint32_t inst_class, uint32_t simd_elem_count)
{
    thread_state_t* st = get_emitting_thread_state();
    if ( st == NULL ) return;
    if ( st->pending_count >= ARIEL_DR_MAX_PENDING_WRITES ) {
        emit_memory_ref(st->core_idx, pc, addr, size, true, inst_class, simd_elem_count);
        return;
    }
    st->pending[st->pending_count].addr = (uint64_t)addr;
    st->pending[st->pending_count].size = size;
    st->pending_count++;
}

static void
record_pending_writes_and_end(app_pc pc, uint32_t inst_class, uint32_t simd_elem_count)
{
    thread_state_t* st = get_emitting_thread_state();
    if ( st == NULL ) return;
    for ( uint32_t i = 0; i < st->pending_count; ++i ) {
        emit_memory_ref(st->core_idx, pc, (app_pc)st->pending[i].addr, st->pending[i].size, true,
            inst_class, simd_elem_count);
    }
    st->pending_count = 0;
    emit_instruction_marker(st->core_idx, ARIEL_END_INSTRUCTION, pc, ARIEL_INST_UNKNOWN, 1);
}

static bool
insert_memref_call(void* drcontext, instrlist_t* bb, instr_t* where, instr_t* instr_operands,
    opnd_t ref, bool is_write, memref_mode_t mode, uint32_t inst_class, uint32_t simd_elem_count)
{
    reg_id_t reg_addr = DR_REG_NULL;
    reg_id_t reg_tmp  = DR_REG_NULL;

    if ( drreg_reserve_register(drcontext, bb, where, NULL, &reg_addr) != DRREG_SUCCESS ) {
        return false;
    }
    if ( drreg_reserve_register(drcontext, bb, where, NULL, &reg_tmp) != DRREG_SUCCESS ) {
        drreg_unreserve_register(drcontext, bb, where, reg_addr);
        return false;
    }

    bool ok = drutil_insert_get_mem_addr(drcontext, bb, where, ref, reg_addr, reg_tmp);
    if ( !ok ) {
        drreg_unreserve_register(drcontext, bb, where, reg_tmp);
        drreg_unreserve_register(drcontext, bb, where, reg_addr);
        return false;
    }

    int sz = drutil_opnd_mem_size_in_bytes(ref, instr_operands);
    if ( sz <= 0 ) sz = 1;

    opnd_t pc_opnd = OPND_CREATE_INTPTR((ptr_int_t)instr_get_app_pc(instr_operands));
    if ( mode == MEMREF_SINGLE ) {
        dr_insert_clean_call(drcontext, bb, where, (void*)record_single_ref, false, 6,
            pc_opnd, opnd_create_reg(reg_addr), OPND_CREATE_INT32(sz), OPND_CREATE_INT32(is_write ? 1 : 0),
            OPND_CREATE_INT32(inst_class), OPND_CREATE_INT32(simd_elem_count));
    }
    else {
        void* func = (void*)record_memory_read;
        if ( mode == MEMREF_WRITE ) func = (void*)record_memory_write;
        else if ( mode == MEMREF_PENDING_WRITE ) func = (void*)record_pending_write;
        dr_insert_clean_call(drcontext, bb, where, func, false, 5,
            pc_opnd, opnd_create_reg(reg_addr), OPND_CREATE_INT32(sz),
            OPND_CREATE_INT32(inst_class), OPND_CREATE_INT32(simd_elem_count));
    }

    drreg_unreserve_register(drcontext, bb, where, reg_tmp);
    drreg_unreserve_register(drcontext, bb, where, reg_addr);
    return true;
}

static bool
parse_uint_arg(const char* key, int argc, const char* argv[], uint32_t* out)
{
    for ( int i = 1; i + 1 < argc; ++i ) {
        if ( strcmp(argv[i], key) == 0 ) {
            char* end = NULL;
            unsigned long val = strtoul(argv[i + 1], &end, 10);
            if ( end == argv[i + 1] || (end != NULL && *end != '\0') ) return false;
            *out = (uint32_t)val;
            return true;
        }
    }
    return false;
}

static void
instrument_memory_instruction(void* drcontext, instrlist_t* bb, instr_t* where, instr_t* instr_operands,
    app_pc pc, uint32_t inst_class, uint32_t simd_elem_count)
{
    int num_refs = 0;
    for ( int i = 0; i < instr_num_srcs(instr_operands); ++i ) {
        if ( opnd_is_memory_reference(instr_get_src(instr_operands, i)) ) num_refs++;
    }
    for ( int i = 0; i < instr_num_dsts(instr_operands); ++i ) {
        if ( opnd_is_memory_reference(instr_get_dst(instr_operands, i)) ) num_refs++;
    }

    // With payload tracing, write data is read after the instruction executes so that
    // the payload holds the stored value. This needs a fall-through insertion point.
    instr_t* after       = instr_get_next(where);
    bool     defer_write = write_payload && instr_writes_memory(instr_operands) && after != NULL &&
                       !instr_is_cti(where);

    if ( num_refs == 1 && !defer_write ) {
        for ( int i = 0; i < instr_num_srcs(instr_operands); ++i ) {
            opnd_t src = instr_get_src(instr_operands, i);
            if ( opnd_is_memory_reference(src) &&
                 insert_memref_call(drcontext, bb, where, instr_operands, src, false, MEMREF_SINGLE, inst_class,
                     simd_elem_count) ) {
                return;
            }
        }
        for ( int i = 0; i < instr_num_dsts(instr_operands); ++i ) {
            opnd_t dst = instr_get_dst(instr_operands, i);
            if ( opnd_is_memory_reference(dst) &&
                 insert_memref_call(drcontext, bb, where, instr_operands, dst, true, MEMREF_SINGLE, inst_class,
                     simd_elem_count) ) {
                return;
            }
        }
        // Address could not be computed (e.g. vector addressing): report as a no-op like Pin
        dr_insert_clean_call(drcontext, bb, where, (void*)record_noops, false, 2,
            OPND_CREATE_INTPTR((ptr_int_t)pc), OPND_CREATE_INT32(1));
        return;
    }

    dr_insert_clean_call(drcontext, bb, where, (void*)record_instruction_start, false, 3,
        OPND_CREATE_INTPTR((ptr_int_t)pc),
        OPND_CREATE_INT32(inst_class),
        OPND_CREATE_INT32(simd_elem_count));

    int inserted = 0;
    for ( int i = 0; i < instr_num_srcs(instr_operands); ++i ) {
        const opnd_t src = instr_get_src(instr_operands, i);
        if ( opnd_is_memory_reference(src) &&
             insert_memref_call(drcontext, bb, where, instr_operands, src, false, MEMREF_READ, inst_class,
                 simd_elem_count) ) {
            inserted++;
        }
    }

    for ( int i = 0; i < instr_num_dsts(instr_operands); ++i ) {
        const opnd_t dst = instr_get_dst(instr_operands, i);
        if ( opnd_is_memory_reference(dst) &&
             insert_memref_call(drcontext, bb, where, instr_operands, dst, true,
                 defer_write ? MEMREF_PENDING_WRITE : MEMREF_WRITE, inst_class, simd_elem_count) ) {
            inserted++;
        }
    }

    if ( defer_write ) {
        dr_insert_clean_call(drcontext, bb, after, (void*)record_pending_writes_and_end, false, 3,
            OPND_CREATE_INTPTR((ptr_int_t)pc),
            OPND_CREATE_INT32(inst_class),
            OPND_CREATE_INT32(simd_elem_count));
    }
    else {
        dr_insert_clean_call(drcontext, bb, where, (void*)record_instruction_end, false, 1,
            OPND_CREATE_INTPTR((ptr_int_t)pc));
    }

    if ( inserted == 0 ) {
        dr_insert_clean_call(drcontext, bb, defer_write ? after : where, (void*)record_noops, false, 2,
            OPND_CREATE_INTPTR((ptr_int_t)pc), OPND_CREATE_INT32(1));
    }
}

static dr_emit_flags_t
event_app_instruction(void* drcontext, void* tag, instrlist_t* bb, instr_t* where,
    bool for_trace, bool translating, void* user_data)
{
    (void)tag;
    (void)for_trace;
    (void)translating;
    (void)user_data;

    instr_t* instr_operands = drmgr_orig_app_instr_for_operands(drcontext);
    if ( instr_operands == NULL ) return DR_EMIT_DEFAULT;

    app_pc pc = instr_get_app_pc(instr_operands);

    inst_kind_t kind = get_inst_kind(instr_operands);
    if ( kind == INST_KIND_PLAIN ) {
        // Consecutive plain instructions are reported with a single clean call at the
        // head of the run; the remaining instructions in the run insert nothing.
        instr_t* prev = instr_get_prev_app(where);
        if ( prev != NULL && get_inst_kind(prev) == INST_KIND_PLAIN ) return DR_EMIT_DEFAULT;

        uint32_t run = 1;
        for ( instr_t* next = instr_get_next_app(where); next != NULL && get_inst_kind(next) == INST_KIND_PLAIN;
              next = instr_get_next_app(next) ) {
            run++;
        }
        dr_insert_clean_call(drcontext, bb, where, (void*)record_noops, false, 2,
            OPND_CREATE_INTPTR((ptr_int_t)pc), OPND_CREATE_INT32(run));
        return DR_EMIT_DEFAULT;
    }

    uint32_t inst_class = ARIEL_INST_UNKNOWN;
    uint32_t simd_elem_count = 1;
    classify_instruction(instr_operands, &inst_class, &simd_elem_count);

    if ( kind == INST_KIND_FP_COMPUTE ) {
        dr_insert_clean_call(drcontext, bb, where, (void*)record_fp_compute, false, 3,
            OPND_CREATE_INTPTR((ptr_int_t)pc),
            OPND_CREATE_INT32(inst_class),
            OPND_CREATE_INT32(simd_elem_count));
        return DR_EMIT_DEFAULT;
    }

    instrument_memory_instruction(drcontext, bb, where, instr_operands, pc, inst_class, simd_elem_count);
    return DR_EMIT_DEFAULT;
}

static const char*
parse_str_arg(const char* key, int argc, const char* argv[])
{
    for ( int i = 1; i + 1 < argc; ++i ) {
        if ( strcmp(argv[i], key) == 0 ) return argv[i + 1];
    }
    return NULL;
}

} // namespace

DR_EXPORT void
dr_client_main(client_id_t id, int argc, const char* argv[])
{
    (void)id;
    tls_idx   = -1;

    const char* shm_name = parse_str_arg("-shm_name", argc, argv);
    if ( shm_name == NULL ) {
        dr_printf("SSTARIEL-DR: missing required -shm_name argument\n");
        dr_abort();
    }

    uint32_t parsed_cores = 0;
    if ( !parse_uint_arg("-core_count", argc, argv, &parsed_cores) || parsed_cores == 0 ) {
        dr_printf("SSTARIEL-DR: missing/invalid -core_count argument\n");
        dr_abort();
    }
    max_cores = parsed_cores;

    uint32_t payload = 0;
    if ( parse_uint_arg("-write_payload", argc, argv, &payload) ) {
        write_payload = (payload != 0);
    }

    tunnel_manager = new ArielTunnelDR(shm_name);
    tunnel         = tunnel_manager->getTunnel();

    if ( !drmgr_init() ) dr_abort();

    drreg_options_t ops = { sizeof(ops), 2, false };
    if ( drreg_init(&ops) != DRREG_SUCCESS ) dr_abort();
    if ( !drutil_init() ) dr_abort();

    tls_idx = drmgr_register_tls_field();
    if ( tls_idx == -1 ) dr_abort();

    mapping_lock = dr_mutex_create();
    if ( mapping_lock == NULL ) dr_abort();

    // DynamoRIO 11.9x adds drmgr_register_exit_event and drmgr.h then poisons
    // dr_register_exit_event with a macro; older releases (e.g. 11.3) only have
    // dr_register_exit_event.
#if defined(dr_register_exit_event)
    drmgr_register_exit_event(event_exit);
#else
    dr_register_exit_event(event_exit);
#endif
    drmgr_register_thread_init_event(event_thread_init);
    drmgr_register_thread_exit_event(event_thread_exit);
    if ( !drmgr_register_bb_instrumentation_event(NULL, event_app_instruction, NULL) ) dr_abort();
}
