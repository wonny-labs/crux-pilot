#include <crux/execution.h>
#include <errno.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <time.h>

#define CHECK(value)                                                                               \
    do {                                                                                           \
        if (!(value)) {                                                                            \
            fprintf(stderr, "FAIL line=%d %s\n", __LINE__, #value);                                \
            return 1;                                                                              \
        }                                                                                          \
    } while (0)

static int fail_thread;
static int fail_mutex;
static int fail_condition;
static int overflow_clock;
static size_t observed_stack;
/* GNU ld --wrap requires these reserved linker symbol names. */
// NOLINTBEGIN(bugprone-reserved-identifier)
int __real_pthread_create(pthread_t *, const pthread_attr_t *, void *(*)(void *), void *);
int __real_pthread_mutex_init(pthread_mutex_t *, const pthread_mutexattr_t *);
int __real_pthread_cond_init(pthread_cond_t *, const pthread_condattr_t *);
int __real_clock_gettime(clockid_t, struct timespec *);
int __wrap_pthread_create(pthread_t *id, const pthread_attr_t *attr, void *(*entry)(void *),
                          void *arg) {
    if (fail_thread) {
        fail_thread = 0;
        return EAGAIN;
    }
    if (pthread_attr_getstacksize(attr, &observed_stack) != 0) {
        return EINVAL;
    }
    return __real_pthread_create(id, attr, entry, arg);
}
int __wrap_pthread_mutex_init(pthread_mutex_t *mutex, const pthread_mutexattr_t *attr) {
    if (fail_mutex) {
        fail_mutex = 0;
        return ENOMEM;
    }
    return __real_pthread_mutex_init(mutex, attr);
}
int __wrap_pthread_cond_init(pthread_cond_t *cond, const pthread_condattr_t *attr) {
    if (fail_condition) {
        fail_condition = 0;
        return ENOMEM;
    }
    return __real_pthread_cond_init(cond, attr);
}
int __wrap_clock_gettime(clockid_t clock, struct timespec *value) {
    if (overflow_clock) {
        value->tv_sec = (time_t)INT64_MAX;
        value->tv_nsec = 999999999L;
        return 0;
    }
    return __real_clock_gettime(clock, value);
}
// NOLINTEND(bugprone-reserved-identifier)

static void noop(void *argument) {
    (void)argument;
}
int main(void) {
    crux_thread_options_t options = {.priority = CRUX_THREAD_PRIORITY_NORMAL};
    crux_thread_t thread;
    options.stack_size_bytes = 32768;
    CHECK(crux_thread_create(&options, noop, NULL, &thread) == CRUX_OK);
    CHECK(observed_stack == 65536);
    CHECK(crux_thread_join(&thread, 1000) == CRUX_OK);
    options.stack_size_bytes = 65537;
    CHECK(crux_thread_create(&options, noop, NULL, &thread) == CRUX_ERR_OUT_OF_RANGE);
    options.stack_size_bytes = 0;
    fail_thread = 1;
    CHECK(crux_thread_create(&options, noop, NULL, &thread) == CRUX_ERR_NO_RESOURCES);
    CHECK(thread.slot == 0 && thread.generation == 0);
    crux_thread_t workers[4];
    for (size_t i = 0; i < 4; ++i)
        CHECK(crux_thread_create(&options, noop, NULL, &workers[i]) == CRUX_OK);
    for (size_t i = 0; i < 4; ++i)
        CHECK(crux_thread_join(&workers[i], 1000) == CRUX_OK);
    crux_mutex_t mutex;
    fail_mutex = 1;
    CHECK(crux_mutex_create(&mutex) == CRUX_ERR_NO_RESOURCES);
    CHECK(mutex.slot == 0 && mutex.generation == 0);
    crux_mutex_t mutexes[8];
    for (size_t i = 0; i < 8; ++i)
        CHECK(crux_mutex_create(&mutexes[i]) == CRUX_OK);
    for (size_t i = 0; i < 8; ++i)
        CHECK(crux_mutex_destroy(&mutexes[i]) == CRUX_OK);
    crux_semaphore_t semaphore;
    fail_condition = 1;
    CHECK(crux_semaphore_create(0, 1, &semaphore) == CRUX_ERR_NO_RESOURCES);
    CHECK(semaphore.slot == 0 && semaphore.generation == 0);
    crux_semaphore_t semaphores[8];
    for (size_t i = 0; i < 8; ++i)
        CHECK(crux_semaphore_create(0, 1, &semaphores[i]) == CRUX_OK);
    for (size_t i = 0; i < 8; ++i)
        CHECK(crux_semaphore_destroy(&semaphores[i]) == CRUX_OK);
    CHECK(crux_thread_create(&options, noop, NULL, &thread) == CRUX_OK);
    overflow_clock = 1;
    CHECK(crux_thread_join(&thread, 1) == CRUX_ERR_OUT_OF_RANGE);
    overflow_clock = 0;
    CHECK(crux_thread_join(&thread, 1000) == CRUX_OK);
    puts("EXECUTION_FAILURES rollback and deadline overflow passed");
    return 0;
}
