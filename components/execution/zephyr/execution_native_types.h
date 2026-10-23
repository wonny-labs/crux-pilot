#ifndef CRUX_EXECUTION_NATIVE_TYPES_H
#define CRUX_EXECUTION_NATIVE_TYPES_H
#include <stdbool.h>
#include <zephyr/kernel.h>

typedef struct {
    struct k_thread object;
    void (*entry)(void *);
    void *argument;
} exec_native_thread_t;
typedef struct {
    k_timeout_t timeout;
    bool poll;
} exec_native_deadline_t;
typedef k_tid_t exec_native_identity_t;
typedef struct k_mutex exec_native_mutex_t;
typedef struct {
    struct k_sem object;
    uint32_t maximum;
} exec_native_semaphore_t;
#endif
