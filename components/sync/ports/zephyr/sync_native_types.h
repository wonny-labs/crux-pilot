#ifndef CRUX_SYNC_NATIVE_TYPES_H
#define CRUX_SYNC_NATIVE_TYPES_H
#include <stdatomic.h>
#include <stdbool.h>
#include <zephyr/kernel.h>

typedef struct {
    k_timeout_t timeout;
    bool poll;
} sync_native_deadline_t;
typedef k_tid_t sync_native_identity_t;
typedef struct k_mutex sync_native_mutex_t;
typedef struct {
    struct k_sem object;
    atomic_uint tokens; /* Reserved by give before k_sem_give, removed by take after k_sem_take. */
    uint32_t maximum;
} sync_native_semaphore_t;
#endif
