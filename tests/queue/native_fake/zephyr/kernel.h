#ifndef TEST_QUEUE_KERNEL_H
#define TEST_QUEUE_KERNEL_H
#include <limits.h>
#include <stdbool.h>
#include <stdint.h>
typedef struct {
    int64_t ticks;
} k_timeout_t;
struct k_mutex {
    bool owned;
};
struct k_condvar {
    int unused;
};
#define K_MUTEX_DEFINE(name) struct k_mutex name
#define K_NO_WAIT ((k_timeout_t){0})
#define K_FOREVER ((k_timeout_t){-1})
#define K_TIMEOUT_ABS_TICKS(t) ((k_timeout_t){(t) > INT64_MAX - 1 ? INT64_MAX - 1 : (t)})
bool k_is_in_isr(void);
int64_t k_uptime_ticks(void);
static inline int64_t k_ms_to_ticks_ceil64(uint32_t ms)
{
    return ((int64_t)ms * 128 + 999) / 1000;
}
int k_mutex_lock(struct k_mutex *mutex, k_timeout_t timeout);
int k_mutex_unlock(struct k_mutex *mutex);
int k_condvar_init(struct k_condvar *cond);
int k_condvar_wait(struct k_condvar *cond, struct k_mutex *mutex, k_timeout_t timeout);
int k_condvar_broadcast(struct k_condvar *cond);
#endif
