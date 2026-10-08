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

#include <sst_config.h>

#include "dynamoriofrontend.h"

#include <signal.h>
#if !defined(SST_COMPILE_MACOSX)
#include <sys/prctl.h>
#endif
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>

#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define ARIEL_INNER_STRINGIZE(input) #input
#define ARIEL_STRINGIZE(input) ARIEL_INNER_STRINGIZE(input)

using namespace SST::ArielComponent;

DynamoRIOFrontend::DynamoRIOFrontend(ComponentId_t id, Params& params, uint32_t cores,
    uint32_t maxCoreQueueLen, uint32_t defMemPool) :
    ArielFrontendCommon(id, params, cores, maxCoreQueueLen, defMemPool)
{
    verbosemode = params.find<int>("verbose", 0);
    output      = new SST::Output("DynamoRIOFrontend[@f:@l:@p] ", verbosemode, 0, SST::Output::STDOUT);

    parseCommonSubComponentParams(params);
    parseDynamoRIOParams(params);

    tunnelmgr = new SST::Core::Interprocess::MMAPParent<ArielTunnel>(id, core_count, maxCoreQueueLen);
    std::string shmem_region_name = tunnelmgr->getRegionName();
    tunnel                        = tunnelmgr->getTunnel();
    output->verbose(CALL_INFO, 1, 0, "Base pipe name: %s\n", shmem_region_name.c_str());

    setForkArguments();
    app_name = (mpimode == 1) ? mpilauncher : applauncher;

    output->verbose(CALL_INFO, 1, 0, "Completed initialization of the Ariel CPU.\n");
}

DynamoRIOFrontend::~DynamoRIOFrontend() { delete tunnelmgr; }

void
DynamoRIOFrontend::emergencyShutdown()
{
    delete tunnelmgr;
    ArielFrontendCommon::emergencyShutdown();
}

int
DynamoRIOFrontend::forkChildProcess(const char* app, char** args, std::map<std::string, std::string>& app_env,
    ariel_redirect_info_t redirect_info)
{
    if ( isSimulationRunModeInit() ) return 0;

    pid_t the_child = fork();
    if ( the_child < 0 ) {
        perror("fork");
        output->fatal(CALL_INFO, 1,
            "Fork failed to launch the traced process. errno = %d, errstr = %s\n", errno, strerror(errno));
    }

    if ( the_child != 0 ) {
        child_pid = the_child;
        sleep(1);
        int   pstat;
        pid_t check = waitpid(the_child, &pstat, WNOHANG);
        if ( check > 0 ) {
            if ( WIFEXITED(pstat) == true ) {
                output->fatal(CALL_INFO, 1,
                    "Launching trace child failed! Child exited with status %d\n", WEXITSTATUS(pstat));
            }
            else if ( WIFSIGNALED(pstat) == true ) {
                output->fatal(CALL_INFO, 1,
                    "Launching trace child failed! Child terminated with signal %d; core dump file created = %d\n",
                    WTERMSIG(pstat), WCOREDUMP(pstat));
            }
            else if ( WIFSTOPPED(pstat) == true ) {
                output->fatal(CALL_INFO, 1,
                    "Launching trace child failed! Child stopped with signal %d\n", WSTOPSIG(pstat));
            }
            else {
                output->fatal(CALL_INFO, 1, "Launching trace child failed! Unknown problem; pstat = %d\n", pstat);
            }
        }
        else if ( check < 0 ) {
            perror("waitpid");
            output->fatal(CALL_INFO, 1,
                "Waitpid returned an error, errno = %d. Did the child ever even start?\n", errno);
        }
        return (int)the_child;
    }

    if ( "" != redirect_info.stdin_file ) {
        if ( !freopen(redirect_info.stdin_file.c_str(), "r", stdin) ) {
            output->fatal(CALL_INFO, 1, 0, "Failed to redirect stdin\n");
        }
    }
    if ( "" != redirect_info.stdout_file ) {
        std::string mode = "w+";
        if ( redirect_info.stdoutappend ) mode = "a+";
        if ( !freopen(redirect_info.stdout_file.c_str(), mode.c_str(), stdout) ) {
            output->fatal(CALL_INFO, 1, 0, "Failed to redirect stdout\n");
        }
    }
    if ( "" != redirect_info.stderr_file ) {
        std::string mode = "w+";
        if ( redirect_info.stderrappend ) mode = "a+";
        if ( !freopen(redirect_info.stderr_file.c_str(), mode.c_str(), stderr) ) {
            output->fatal(CALL_INFO, 1, 0, "Failed to redirect stderr\n");
        }
    }

#if !defined(SST_COMPILE_MACOSX)
#if defined(HAVE_SET_PTRACER)
    prctl(PR_SET_PTRACER, getppid(), 0, 0, 0);
#endif
#endif

    if ( app_env.size() == 0 ) {
        int ret_code = execvp(app, args);
        perror("execvp");
        output->verbose(CALL_INFO, 1, 0, "Call to execvp returned: %d\n", ret_code);
        output->fatal(CALL_INFO, -1, "Error executing: %s under a DynamoRIO fork\n", app);
    }
    else {
        char**   execute_env_cp  = (char**)malloc(sizeof(char*) * (app_env.size() + 1));
        uint32_t next_env_cp_idx = 0;
        for (auto env_itr = app_env.begin(); env_itr != app_env.end(); env_itr++) {
            size_t nv_pair_size      = sizeof(char) * (2 + env_itr->first.size() + env_itr->second.size());
            char*  execute_env_nv    = (char*)malloc(nv_pair_size);
            snprintf(execute_env_nv, nv_pair_size, "%s=%s", env_itr->first.c_str(), env_itr->second.c_str());
            execute_env_cp[next_env_cp_idx] = execute_env_nv;
            next_env_cp_idx++;
        }
        execute_env_cp[app_env.size()] = NULL;

        int ret_code = execve(app, args, execute_env_cp);
        perror("execve");
        output->verbose(CALL_INFO, 1, 0, "Call to execve returned: %d\n", ret_code);
        output->fatal(CALL_INFO, -1, "Error executing %s under a DynamoRIO fork\n", app);
    }
    return 0;
}

void
DynamoRIOFrontend::setForkArguments()
{
    uint32_t mpi_arg_count = 0;
    if ( mpimode == 1 ) mpi_arg_count = 3;

    const uint32_t dr_arg_count = 11 + launch_param_count;

    execute_args = (char**)malloc(sizeof(char*) * (mpi_arg_count + dr_arg_count + appargcount + 1));

    uint32_t arg = 0;
    if ( mpimode == 1 ) {
        std::string mpiranks_str     = std::to_string(mpiranks);
        std::string mpitracerank_str = std::to_string(mpitracerank);

        size_t mpilauncher_size = sizeof(char) * (mpilauncher.size() + 2);
        execute_args[arg]       = (char*)malloc(mpilauncher_size);
        snprintf(execute_args[arg], mpilauncher_size, "%s", mpilauncher.c_str());
        arg++;

        size_t mpiranks_size = sizeof(char) * (mpiranks_str.size() + 2);
        execute_args[arg]    = (char*)malloc(mpiranks_size);
        snprintf(execute_args[arg], mpiranks_size, "%s", mpiranks_str.c_str());
        arg++;

        size_t tracerank_size = sizeof(char) * (mpitracerank_str.size() + 2);
        execute_args[arg]     = (char*)malloc(tracerank_size);
        snprintf(execute_args[arg], tracerank_size, "%s", mpitracerank_str.c_str());
        arg++;
    }

    execute_args[arg] = (char*)malloc(sizeof(char) * (applauncher.size() + 2));
    snprintf(execute_args[arg], applauncher.size() + 2, "%s", applauncher.c_str());
    arg++;

    execute_args[arg++] = const_cast<char*>("-c");
    execute_args[arg]   = (char*)malloc(sizeof(char) * (arieltool.size() + 1));
    strcpy(execute_args[arg], arieltool.c_str());
    arg++;

    for (auto itr = launch_params.begin(); itr != launch_params.end(); itr++) {
        std::string launch_p = (*itr);
        execute_args[arg]    = (char*)malloc(sizeof(char) * (launch_p.size() + 1));
        strcpy(execute_args[arg], launch_p.c_str());
        arg++;
    }

    std::string shmem_region_name = tunnelmgr->getRegionName();
    size_t      buff8size         = sizeof(char) * 32;

    execute_args[arg++] = const_cast<char*>("-shm_name");
    execute_args[arg]   = (char*)malloc(sizeof(char) * (shmem_region_name.size() + 1));
    strcpy(execute_args[arg], shmem_region_name.c_str());
    arg++;

    execute_args[arg++] = const_cast<char*>("-core_count");
    execute_args[arg]   = (char*)malloc(buff8size);
    snprintf(execute_args[arg], buff8size, "%" PRIu32, core_count);
    arg++;

    execute_args[arg++] = const_cast<char*>("-write_payload");
    execute_args[arg]   = (char*)malloc(buff8size);
    snprintf(execute_args[arg], buff8size, "%d", writepayloadtrace);
    arg++;

    execute_args[arg++] = const_cast<char*>("--");

    execute_args[arg] = (char*)malloc(sizeof(char) * (executable.size() + 1));
    strcpy(execute_args[arg], executable.c_str());
    arg++;

    for (auto itr = app_arguments.begin(); itr != app_arguments.end(); itr++) {
        std::string app_arg = (*itr);
        execute_args[arg]   = (char*)malloc(sizeof(char) * (app_arg.size() + 1));
        strcpy(execute_args[arg], app_arg.c_str());
        arg++;
    }

    execute_args[arg] = NULL;
}

void
DynamoRIOFrontend::parseDynamoRIOParams(Params& params)
{
    applauncher = params.find<std::string>("launcher", DYNAMORIO_EXECUTABLE);

    launch_param_count          = (uint32_t)params.find<uint32_t>("launchparamcount", 0);
    size_t param_name_buff_size = sizeof(char) * 512;
    char*  param_name_buffer    = (char*)malloc(param_name_buff_size);

    for ( uint32_t aa = 0; aa < launch_param_count; aa++ ) {
        snprintf(param_name_buffer, param_name_buff_size, "launchparam%" PRIu32, aa);
        std::string launch_p = params.find<std::string>(param_name_buffer, "");
        if ( "" == launch_p ) {
            output->fatal(CALL_INFO, -1,
                "Error: launch parameter %" PRIu32 " is empty string, this must be set to a value.\n", aa);
        }
        launch_params.push_back(launch_p);
    }
    free(param_name_buffer);

    size_t tool_path_size = sizeof(char) * 1024;
    char*  tool_path      = (char*)malloc(tool_path_size);
    snprintf(tool_path, tool_path_size, "%s/fedynamorio.so", ARIEL_STRINGIZE(ARIEL_TOOL_DIR));
    arieltool = params.find<std::string>("arieltool", tool_path);
    free(tool_path);

    if ( "" == arieltool ) {
        output->fatal(CALL_INFO, -1,
            "The arieltool parameter specifying which DynamoRIO client to run was not specified\n");
    }

    writepayloadtrace = (params.find<int>("writepayloadtrace") == 0) ? 0 : 1;

    if ( mpimode == 1 ) {
        output->fatal(CALL_INFO, -1,
            "The DynamoRIO frontend does not support mpimode in Milestone 1.\n");
    }
}
