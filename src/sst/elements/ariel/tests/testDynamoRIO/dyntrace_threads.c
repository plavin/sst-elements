// Copyright 2009-2026 NTESS.

#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

typedef struct {
    int thread_id;
    volatile uint64_t* data;
    int len;
} worker_args_t;

static void*
worker(void* arg)
{
    worker_args_t* a = (worker_args_t*)arg;
    for ( int i = 0; i < a->len; ++i ) {
        a->data[i] += (uint64_t)(a->thread_id + 1);
    }
    return NULL;
}

int
main(int argc, char** argv)
{
    int threads = 2;
    if ( argc > 1 ) {
        threads = atoi(argv[1]);
        if ( threads < 1 ) threads = 1;
        if ( threads > 8 ) threads = 8;
    }

    const int len = 1024;
    volatile uint64_t* data = (volatile uint64_t*)malloc(sizeof(uint64_t) * len);
    if ( data == NULL ) return 1;

    for ( int i = 0; i < len; ++i ) {
        data[i] = (uint64_t)i;
    }

    pthread_t tids[8];
    worker_args_t args[8];

    for ( int t = 0; t < threads; ++t ) {
        args[t].thread_id = t;
        args[t].data = data;
        args[t].len = len;
        if ( pthread_create(&tids[t], NULL, worker, &args[t]) != 0 ) return 2;
    }

    for ( int t = 0; t < threads; ++t ) {
        pthread_join(tids[t], NULL);
    }

    uint64_t sum = 0;
    for ( int i = 0; i < len; ++i ) {
        sum += data[i];
    }

    printf("dyntrace_threads sum=%llu\n", (unsigned long long)sum);
    free((void*)data);
    return 0;
}
