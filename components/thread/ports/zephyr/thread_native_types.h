#ifndef CRUX_THREAD_NATIVE_TYPES_H
#define CRUX_THREAD_NATIVE_TYPES_H
#include <stdbool.h>
#include <zephyr/kernel.h>

typedef struct {
    struct k_thread object;
    void (*entry)(void *);
    void *argument;
} thread_native_thread_t;

typedef struct {
    k_timeout_t timeout;
    bool poll;
} thread_native_deadline_t;

#endif
