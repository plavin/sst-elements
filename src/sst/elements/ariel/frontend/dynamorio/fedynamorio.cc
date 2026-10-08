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
#include "drmgr.h"
#include "drreg.h"
#include "drutil.h"

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

struct thread_state_t {
    uint32_t core_idx;
    bool     emit;
};

static int         tls_idx;
static void*       mapping_lock;
static uint32_t    max_cores          = 1;
static uint32_t    next_core_id       = 0;
static bool        write_payload      = false;
static ArielTunnelDR* tunnel_manager  = NULL;
static ArielTunnel*   tunnel          = NULL;

static dr_emit_flags_t event_app_instruction(void* drcontext, void* tag, instrlist_t* bb,
    instr_t* where, bool for_trace, bool translating, void* user_data);

static uint32_t
get_or_assign_core(void)
{
    uint32_t assigned = 0;
    dr_mutex_lock(mapping_lock);
    if ( next_core_id >= max_cores ) {
        dr_mutex_unlock(mapping_lock);
        dr_printf("SSTARIEL-DR: thread count exceeded configured Ariel corecount (%u)\n", max_cores);
        dr_abort();
    }
    assigned = next_core_id;
    next_core_id++;
    dr_mutex_unlock(mapping_lock);
    return assigned;
}

static inline thread_state_t*
get_thread_state(void* drcontext)
{
    return (thread_state_t*)drmgr_get_tls_field(drcontext, tls_idx);
}

static void
emit_instruction_marker(uint32_t core_idx, ArielShmemCmd_t marker, app_pc pc)
{
    ArielCommand ac;
    memset(&ac, 0, sizeof(ac));
    ac.command = marker;
    ac.instPtr = (uint64_t)pc;
    tunnel->writeMessage(core_idx, ac);
}

static void
emit_memory_ref(uint32_t core_idx, app_pc pc, app_pc addr, uint32_t size, bool is_write)
{
    ArielCommand ac;
    memset(&ac, 0, sizeof(ac));
    ac.command = is_write ? ARIEL_PERFORM_WRITE : ARIEL_PERFORM_READ;
    ac.instPtr = (uint64_t)pc;
    ac.inst.addr = (uint64_t)addr;
    ac.inst.size = size;
    ac.inst.instClass = 0;
    ac.inst.simdElemCount = 1;
    if ( write_payload ) {
        ac.inst.size = (size > ARIEL_MAX_PAYLOAD_SIZE) ? ARIEL_MAX_PAYLOAD_SIZE : size;
    }
    tunnel->writeMessage(core_idx, ac);
}

static void
event_exit(void)
{
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
    st->core_idx       = get_or_assign_core();
    st->emit           = true;
    drmgr_set_tls_field(drcontext, tls_idx, st);

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
record_instruction_start(void* drcontext, app_pc pc)
{
    thread_state_t* st = get_thread_state(drcontext);
    if ( st == NULL || !st->emit ) return;
    emit_instruction_marker(st->core_idx, ARIEL_START_INSTRUCTION, pc);
}

static void
record_instruction_end(void* drcontext, app_pc pc)
{
    thread_state_t* st = get_thread_state(drcontext);
    if ( st == NULL || !st->emit ) return;
    emit_instruction_marker(st->core_idx, ARIEL_END_INSTRUCTION, pc);
}

static void
record_memory_read(void* drcontext, app_pc pc, app_pc addr, uint32_t size)
{
    thread_state_t* st = get_thread_state(drcontext);
    if ( st == NULL || !st->emit ) return;
    emit_memory_ref(st->core_idx, pc, addr, size, false);
}

static void
record_memory_write(void* drcontext, app_pc pc, app_pc addr, uint32_t size)
{
    thread_state_t* st = get_thread_state(drcontext);
    if ( st == NULL || !st->emit ) return;
    emit_memory_ref(st->core_idx, pc, addr, size, true);
}

static bool
insert_memref_call(void* drcontext, instrlist_t* bb, instr_t* where, instr_t* instr_operands,
    opnd_t ref, bool is_write)
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

    dr_insert_clean_call(drcontext, bb, where,
        (void*)(is_write ? record_memory_write : record_memory_read),
        false, 3,
        OPND_CREATE_INTPTR((ptr_int_t)instr_get_app_pc(instr_operands)),
        opnd_create_reg(reg_addr),
        OPND_CREATE_INT32(sz));

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

    if ( !instr_reads_memory(instr_operands) && !instr_writes_memory(instr_operands) ) {
        return DR_EMIT_DEFAULT;
    }

    app_pc pc = instr_get_app_pc(instr_operands);
    dr_insert_clean_call(drcontext, bb, where, (void*)record_instruction_start, false, 1,
        OPND_CREATE_INTPTR((ptr_int_t)pc));

    int i;
    for ( i = 0; i < instr_num_srcs(instr_operands); ++i ) {
        const opnd_t src = instr_get_src(instr_operands, i);
        if ( opnd_is_memory_reference(src) ) {
            insert_memref_call(drcontext, bb, where, instr_operands, src, false);
        }
    }

    for ( i = 0; i < instr_num_dsts(instr_operands); ++i ) {
        const opnd_t dst = instr_get_dst(instr_operands, i);
        if ( opnd_is_memory_reference(dst) ) {
            insert_memref_call(drcontext, bb, where, instr_operands, dst, true);
        }
    }

    dr_insert_clean_call(drcontext, bb, where, (void*)record_instruction_end, false, 1,
        OPND_CREATE_INTPTR((ptr_int_t)pc));

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

    dr_register_exit_event(event_exit);
    drmgr_register_thread_init_event(event_thread_init);
    drmgr_register_thread_exit_event(event_thread_exit);
    if ( !drmgr_register_bb_instrumentation_event(NULL, event_app_instruction, NULL) ) dr_abort();

    dr_printf(
        "SSTARIEL-DR: initialized with %u cores, write_payload=%d\n",
        max_cores, write_payload ? 1 : 0);
}
