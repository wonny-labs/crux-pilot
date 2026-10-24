#ifndef CRUX_SYNC_NATIVE_TYPES_H
#define CRUX_SYNC_NATIVE_TYPES_H

#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>
#include <time.h>

typedef struct {
    struct timespec absolute;
    bool poll;
} sync_native_deadline_t;

typedef pthread_t sync_native_identity_t;
typedef pthread_mutex_t sync_native_mutex_t;
typedef struct {
    pthread_mutex_t lock;
    pthread_cond_t changed;
    uint32_t count;
    uint32_t maximum;
} sync_native_semaphore_t;

#endif
