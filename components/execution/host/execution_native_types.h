#ifndef CRUX_EXECUTION_NATIVE_TYPES_H
#define CRUX_EXECUTION_NATIVE_TYPES_H

#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>
#include <time.h>

typedef struct {
    pthread_t id;
    void (*entry)(void *);
    void *argument;
} exec_native_thread_t;

typedef struct {
    struct timespec absolute;
    bool poll;
} exec_native_deadline_t;

typedef pthread_t exec_native_identity_t;
typedef pthread_mutex_t exec_native_mutex_t;
typedef struct {
    pthread_mutex_t lock;
    pthread_cond_t changed;
    uint32_t count;
    uint32_t maximum;
} exec_native_semaphore_t;

#endif
