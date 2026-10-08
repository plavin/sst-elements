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

#include <sst/core/interprocess/tunneldef.h>
#include "ariel_shmem.h"

#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

using namespace SST::ArielComponent;

namespace {

class ArielTunnelDR {
public:
    explicit ArielTunnelDR(const std::string& region_name) :
        shmPtr(NULL),
        fd(-1),
        filename(region_name),
        shmSize(0),
        tunnel(NULL)
    {
        fd = open(filename.c_str(), O_RDWR);
        if ( fd < 0 ) {
            dr_printf("SSTARIEL-DR: failed to open IPC region '%s': %s\n", filename.c_str(), strerror(errno));
            dr_abort();
        }

        shmPtr = mmap(NULL, sizeof(SST::Core::Interprocess::InternalSharedData), PROT_READ, MAP_SHARED, fd, 0);
        if ( shmPtr == MAP_FAILED ) {
            dr_printf("SSTARIEL-DR: initial mmap failed: %s\n", strerror(errno));
            dr_abort();
        }

        tunnel  = new ArielTunnel(shmPtr);
        shmSize = tunnel->getTunnelSize();

        munmap(shmPtr, sizeof(SST::Core::Interprocess::InternalSharedData));

        shmPtr = mmap(NULL, shmSize, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
        if ( shmPtr == MAP_FAILED ) {
            dr_printf("SSTARIEL-DR: full mmap failed: %s\n", strerror(errno));
            dr_abort();
        }
        tunnel->initialize(shmPtr);
    }

    ~ArielTunnelDR()
    {
        delete tunnel;
        if ( shmPtr != NULL && shmPtr != MAP_FAILED ) {
            munmap(shmPtr, shmSize);
        }
        if ( fd >= 0 ) {
            close(fd);
        }
    }

    ArielTunnel* getTunnel() { return tunnel; }

private:
    void* shmPtr;
    int fd;
    std::string filename;
    size_t shmSize;
    ArielTunnel* tunnel;
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

    tls_idx = drmgr_register_tls_field();
    if ( tls_idx == -1 ) dr_abort();

    mapping_lock = dr_mutex_create();
    if ( mapping_lock == NULL ) dr_abort();

    dr_register_exit_event(event_exit);
    drmgr_register_thread_init_event(event_thread_init);
    drmgr_register_thread_exit_event(event_thread_exit);

    dr_printf(
        "SSTARIEL-DR: initialized (skeleton mode) with %u cores, write_payload=%d\n",
        max_cores, write_payload ? 1 : 0);
}
