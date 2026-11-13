/* The pthread calls that the unchanged reboot suites and fakes make, over Zephyr kernel objects.
 *
 * Why not Zephyr's POSIX layer: it needs a C library that is not the host's (CONFIG_POSIX_API selects
 * options that require picolibc or newlib), and the pinned workspace has neither picolibc nor a
 * toolchain that carries it. native_sim then links the host's glibc, whose pthread_create() starts a
 * host thread that the Zephyr scheduler does not know and that enters the kernel at the same time as
 * the simulated one: the kernel's own spinlock checks fail within seconds. These calls make Zephyr
 * threads instead, so that every thread of a case is scheduled by the kernel. Only what the suites
 * use exists. */
#ifndef REBOOT_NATIVE_PTHREAD_H
#define REBOOT_NATIVE_PTHREAD_H

#include <zephyr/kernel.h>

/* The host's headers define pthread types too, and glibc's <time.h> and <sys/types.h> pull them in.
 * They come first, and the names below are renamed after them, so that the two never meet. */
#include <errno.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <time.h>

#define pthread_t reboot_native_pthread_t
#define pthread_attr_t reboot_native_pthread_attr_t
#define pthread_barrierattr_t reboot_native_pthread_barrierattr_t
#define pthread_mutexattr_t reboot_native_pthread_mutexattr_t
#define pthread_mutex_t reboot_native_pthread_mutex_t
#define pthread_barrier_t reboot_native_pthread_barrier_t

typedef unsigned pthread_t;
typedef struct {
    int unused;
} pthread_attr_t;
typedef struct {
    int unused;
} pthread_barrierattr_t;
typedef struct {
    int unused;
} pthread_mutexattr_t;

typedef struct {
    struct k_mutex mutex;
    atomic_t state; /* 0 not set up, 1 being set up, 2 ready: set up by the first lock */
} pthread_mutex_t;
#define PTHREAD_MUTEX_INITIALIZER {.state = ATOMIC_INIT(0)}

typedef struct {
    struct k_mutex mutex;
    struct k_condvar changed;
    unsigned count, waiting, generation;
} pthread_barrier_t;

int pthread_create(pthread_t *thread, const pthread_attr_t *attributes, void *(*start)(void *),
                   void *argument);
int pthread_join(pthread_t thread, void **result);
int pthread_mutex_lock(pthread_mutex_t *mutex);
int pthread_mutex_unlock(pthread_mutex_t *mutex);
int pthread_barrier_init(pthread_barrier_t *barrier, const pthread_barrierattr_t *attributes,
                         unsigned count);
int pthread_barrier_wait(pthread_barrier_t *barrier);
int pthread_barrier_destroy(pthread_barrier_t *barrier);

/* The host's nanosleep() blocks the whole simulation in a host system call. */
int reboot_native_nanosleep(const struct timespec *request, struct timespec *remaining);
#define nanosleep reboot_native_nanosleep

#endif
