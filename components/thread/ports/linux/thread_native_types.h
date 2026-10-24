#ifndef CRUX_THREAD_NATIVE_TYPES_H
#define CRUX_THREAD_NATIVE_TYPES_H

#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>
#include <time.h>

typedef struct {
    pthread_t id;
    void (*entry)(void *);
    void *argument;
} thread_native_thread_t;

typedef struct {
    struct timespec absolute;
    bool poll;
} thread_native_deadline_t;

#endif
