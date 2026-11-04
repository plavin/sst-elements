// Copyright 2009-2026 NTESS.

// Multi-threaded test program for the Ariel DynamoRIO frontend.
// Usage: dyntrace_threads <workers>
// The main thread plus <workers> threads are traced, so run with corecount = workers + 1.
// Each worker updates its own slice of a shared array (so the checksum is deterministic)
// and runs single and double precision FP loops to exercise Ariel's FP statistics.

#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#define MAX_WORKERS 16
#define SLICE_LEN   1024
#define FP_ITERS    256

typedef struct {
    int thread_id;
    volatile uint64_t* data;
    volatile double dp_result;
    volatile float sp_result;
} worker_args_t;

static void*
worker(void* arg)
{
    worker_args_t* a = (worker_args_t*)arg;
    volatile uint64_t* slice = a->data + (a->thread_id * SLICE_LEN);
    for ( int i = 0; i < SLICE_LEN; ++i ) {
        slice[i] += (uint64_t)(a->thread_id + 1);
    }

    double dp = 1.0 + a->thread_id;
    float  sp = 1.0f + a->thread_id;
    for ( int i = 0; i < FP_ITERS; ++i ) {
        dp = dp * 1.0001 + 0.5;
        sp = sp * 1.0001f + 0.5f;
    }
    a->dp_result = dp;
    a->sp_result = sp;
    return NULL;
}

int
main(int argc, char** argv)
{
    int workers = 1;
    if ( argc > 1 ) {
        workers = atoi(argv[1]);
        if ( workers < 1 ) workers = 1;
        if ( workers > MAX_WORKERS ) workers = MAX_WORKERS;
    }

    const int len = workers * SLICE_LEN;
    volatile uint64_t* data = (volatile uint64_t*)malloc(sizeof(uint64_t) * len);
    if ( data == NULL ) return 1;

    for ( int i = 0; i < len; ++i ) {
        data[i] = (uint64_t)i;
    }

    pthread_t tids[MAX_WORKERS];
    worker_args_t args[MAX_WORKERS];

    for ( int t = 0; t < workers; ++t ) {
        args[t].thread_id = t;
        args[t].data = data;
        if ( pthread_create(&tids[t], NULL, worker, &args[t]) != 0 ) return 2;
    }

    for ( int t = 0; t < workers; ++t ) {
        pthread_join(tids[t], NULL);
    }

    uint64_t sum = 0;
    for ( int i = 0; i < len; ++i ) {
        sum += data[i];
    }

    uint64_t expected = (uint64_t)len * (uint64_t)(len - 1) / 2;
    for ( int t = 0; t < workers; ++t ) {
        expected += (uint64_t)SLICE_LEN * (uint64_t)(t + 1);
    }

    printf("dyntrace_threads workers=%d sum=%llu expected=%llu %s\n", workers,
        (unsigned long long)sum, (unsigned long long)expected, (sum == expected) ? "PASS" : "FAIL");
    free((void*)data);
    return (sum == expected) ? 0 : 3;
}
