/* A test-only stand-in for the part of <zephyr/kernel.h> that the Zephyr-direct reboot service
 * uses, so that the Host can build the service's core for the suites that do not need a real
 * kernel: the reboot suites that run on the Host and the web route suites. It is plain pthreads and
 * it is not a Crux interface. Only the calls that components/reboot/ports/zephyr uses exist, and a
 * stub that grows beyond them is the wrong tool: use native_sim.
 *
 * k_mutex_lock() reaches z_impl_k_mutex_lock(), the function that Zephyr's own inline wrapper
 * calls, so that fault injection by --wrap=z_impl_k_mutex_lock is the same on the Host and on
 * native_sim. */
#ifndef CRUX_TEST_HOST_KERNEL_H
#define CRUX_TEST_HOST_KERNEL_H

#include <pthread.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define CONFIG_NUM_PREEMPT_PRIORITIES 15
#define BUILD_ASSERT(condition, message) _Static_assert(condition, message)

/* A timeout is in milliseconds here, and a tick is one of them. */
typedef struct {
    int64_t ticks;
} k_timeout_t;
#define K_NO_WAIT ((k_timeout_t){0})
#define K_FOREVER ((k_timeout_t){-1})
#define K_MSEC(ms) ((k_timeout_t){(int64_t)(ms)})
static inline int64_t k_ticks_to_ms_floor64(int64_t ticks)
{
    return ticks;
}

struct k_mutex {
    pthread_mutex_t lock;
};
#define K_MUTEX_DEFINE(name) struct k_mutex name = {PTHREAD_MUTEX_INITIALIZER}
int z_impl_k_mutex_lock(struct k_mutex *mutex, k_timeout_t timeout);
static inline int k_mutex_lock(struct k_mutex *mutex, k_timeout_t timeout)
{
    return z_impl_k_mutex_lock(mutex, timeout);
}
int k_mutex_unlock(struct k_mutex *mutex);

struct k_sem {
    pthread_mutex_t lock;
    pthread_cond_t changed;
    unsigned count;
    unsigned limit;
};
#define K_SEM_DEFINE(name, initial, maximum)                                                       \
    struct k_sem name = {PTHREAD_MUTEX_INITIALIZER, PTHREAD_COND_INITIALIZER, initial, maximum}
int k_sem_take(struct k_sem *sem, k_timeout_t timeout);
void k_sem_give(struct k_sem *sem);

struct k_thread {
    pthread_t id;
};
typedef char k_thread_stack_t;
#define K_THREAD_STACK_DEFINE(name, size) k_thread_stack_t name[size]
#define K_THREAD_STACK_SIZEOF(stack) sizeof(stack)
typedef void (*k_thread_entry_t)(void *, void *, void *);
void *k_thread_create(struct k_thread *thread, k_thread_stack_t *stack, size_t size,
                      k_thread_entry_t entry, void *p1, void *p2, void *p3, int priority,
                      uint32_t options, k_timeout_t delay);

int32_t k_msleep(int32_t ms);

#endif
