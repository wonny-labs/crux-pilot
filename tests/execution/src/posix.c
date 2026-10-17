#ifndef __ZEPHYR__
#define _POSIX_C_SOURCE 200809L
#endif
#include <errno.h>
#include <fcntl.h>
#include <mqueue.h>
#include <pthread.h>
#include <sched.h>
#include <semaphore.h>
#include <stdint.h>
#include <stdio.h>
#include <time.h>
#ifdef __ZEPHYR__
#include <zephyr/ztest.h>
#define CHECK(value) zassert_true(value, "%s", #value)
#else
#include <assert.h>
#define CHECK(value) assert(value)
#endif

static sem_t signal;
static pthread_mutex_t mutex;
static int worker_result;

static struct timespec after_ms(clockid_t clock, long ms) {
    struct timespec deadline;
    CHECK(clock_gettime(clock, &deadline) == 0);
    deadline.tv_nsec += ms * 1000000L;
    deadline.tv_sec += deadline.tv_nsec / 1000000000L;
    deadline.tv_nsec %= 1000000000L;
    return deadline;
}

static int64_t monotonic_ms(void) {
    struct timespec now;
    CHECK(clock_gettime(CLOCK_MONOTONIC, &now) == 0);
    return (int64_t)now.tv_sec * 1000 + now.tv_nsec / 1000000;
}

static void *notify_worker(void *arg) {
    struct timespec delay = {.tv_nsec = 20000000L};
    CHECK(nanosleep(&delay, NULL) == 0);
#ifdef __ZEPHYR__
    size_t unused = 0;
    CHECK(k_thread_stack_space_get(k_current_get(), &unused) == 0);
    CHECK(unused > 0);
    printf("PROBE posix stack_unused_bytes=%zu\n", unused);
#endif
    CHECK(sem_post(&signal) == 0);
    return arg;
}

static void *mutex_worker(void *arg) {
    (void)arg;
    struct timespec deadline = after_ms(CLOCK_REALTIME, 20);
    worker_result = pthread_mutex_timedlock(&mutex, &deadline);
    if (worker_result == 0) {
        CHECK(pthread_mutex_unlock(&mutex) == 0);
    }
    return NULL;
}

static void test_lifecycle_and_attributes(void) {
    pthread_attr_t attr;
    CHECK(pthread_attr_init(&attr) == 0);
    size_t stack = 0;
    CHECK(pthread_attr_getstacksize(&attr, &stack) == 0);
    CHECK(pthread_attr_setstacksize(&attr, 0) == EINVAL);
    int policy;
    struct sched_param parameters;
    CHECK(pthread_attr_getschedpolicy(&attr, &policy) == 0);
    CHECK(pthread_attr_getschedparam(&attr, &parameters) == 0);
    printf("PROBE posix default_stack=%zu policy=%d priority=%d\n", stack, policy,
           parameters.sched_priority);
    CHECK(pthread_attr_destroy(&attr) == 0);
    CHECK(sem_init(&signal, 0, 0) == 0);
    for (int i = 0; i < 2; ++i) {
        pthread_t thread;
        void *result = NULL;
        CHECK(pthread_create(&thread, NULL, notify_worker, &signal) == 0);
        CHECK(sem_wait(&signal) == 0);
        CHECK(pthread_join(thread, &result) == 0);
        CHECK(result == &signal);
    }
    CHECK(sem_destroy(&signal) == 0);
#ifdef __ZEPHYR__
    pthread_attr_t held[16];
    int count = 0;
    int allocation_result = 0;
    for (; count < 16; ++count) {
        allocation_result = pthread_attr_init(&held[count]);
        if (allocation_result != 0) {
            break;
        }
    }
    CHECK(count > 0 && count < 16);
    CHECK(allocation_result == ENOMEM);
    printf("PROBE posix allocated_stack_slots=%d exhaustion=%d\n", count, allocation_result);
    for (int i = 0; i < count; ++i) {
        CHECK(pthread_attr_destroy(&held[i]) == 0);
    }
    CHECK(pthread_attr_init(&attr) == 0);
    CHECK(pthread_attr_destroy(&attr) == 0);
#endif
}

static void test_mutex_contention(void) {
    pthread_mutexattr_t attr;
    CHECK(pthread_mutexattr_init(&attr) == 0);
#ifdef __ZEPHYR__
    /* Pinned Zephyr rejects the POSIX protocol attribute; do not claim conformance. */
    CHECK(pthread_mutexattr_setprotocol(&attr, PTHREAD_PRIO_INHERIT) == ENOTSUP);
#else
    CHECK(pthread_mutexattr_setprotocol(&attr, PTHREAD_PRIO_INHERIT) == 0);
#endif
    CHECK(pthread_mutex_init(&mutex, &attr) == 0);
    CHECK(pthread_mutexattr_destroy(&attr) == 0);
    CHECK(pthread_mutex_lock(&mutex) == 0);
    pthread_t thread;
    CHECK(pthread_create(&thread, NULL, mutex_worker, NULL) == 0);
    CHECK(pthread_join(thread, NULL) == 0);
    CHECK(worker_result == ETIMEDOUT);
    CHECK(pthread_mutex_unlock(&mutex) == 0);
    CHECK(pthread_create(&thread, NULL, mutex_worker, NULL) == 0);
    CHECK(pthread_join(thread, NULL) == 0);
    CHECK(worker_result == 0);
    CHECK(pthread_mutex_destroy(&mutex) == 0);
}

static void test_semaphore_timeout(void) {
    CHECK(sem_init(&signal, 0, 0) == 0);
    errno = 0;
    CHECK(sem_trywait(&signal) == -1 && errno == EAGAIN);
    struct timespec deadline = after_ms(CLOCK_REALTIME, 10);
    int64_t start = monotonic_ms();
    CHECK(sem_timedwait(&signal, &deadline) == -1 && errno == ETIMEDOUT);
    CHECK(monotonic_ms() - start >= 9);
    pthread_t thread;
    start = monotonic_ms();
    CHECK(pthread_create(&thread, NULL, notify_worker, NULL) == 0);
    deadline = after_ms(CLOCK_REALTIME, 1000);
    CHECK(sem_timedwait(&signal, &deadline) == 0);
    CHECK(monotonic_ms() - start >= 19);
    CHECK(pthread_join(thread, NULL) == 0);
    CHECK(sem_destroy(&signal) == 0);
}

static void test_messages(void) {
    const char *name = "/crux_m2_probe";
    struct mq_attr attr = {.mq_maxmsg = 1, .mq_msgsize = sizeof(uint32_t)};
    mqd_t queue = mq_open(name, O_CREAT | O_EXCL | O_RDWR | O_NONBLOCK, 0600, &attr);
    CHECK(queue != (mqd_t)-1);
    uint32_t sent = 42;
    uint32_t received = 0;
    CHECK(mq_receive(queue, (char *)&received, sizeof(received), NULL) == -1 && errno == EAGAIN);
    CHECK(mq_send(queue, (const char *)&sent, sizeof(sent), 0) == 0);
    CHECK(mq_send(queue, (const char *)&sent, sizeof(sent), 0) == -1 && errno == EAGAIN);
    CHECK(mq_receive(queue, (char *)&received, sizeof(received), NULL) == sizeof(received));
    CHECK(received == sent);
    CHECK(mq_close(queue) == 0);
    CHECK(mq_unlink(name) == 0);
}

static void test_periodic_monotonic(void) {
    int64_t start = monotonic_ms();
    int64_t next = start + 10;
    unsigned int skipped = 0;
    for (int i = 0; i < 6; ++i) {
        struct timespec deadline = {.tv_sec = next / 1000, .tv_nsec = (next % 1000) * 1000000};
        CHECK(clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &deadline, NULL) == 0);
        int64_t released = monotonic_ms();
        CHECK(released >= next);
        printf("PROBE posix cycle=%d lateness_ms=%lld\n", i, (long long)(released - next));
        if (i == 1) {
            struct timespec work = {.tv_nsec = 25000000L};
            CHECK(nanosleep(&work, NULL) == 0);
        }
        next += 10;
        int64_t now = monotonic_ms();
        while (next <= now) {
            next += 10;
            ++skipped;
        }
    }
    CHECK(skipped >= 2);
    printf("PROBE posix periodic elapsed_ms=%lld skipped=%u\n", (long long)(monotonic_ms() - start),
           skipped);
}

#ifdef __ZEPHYR__
ZTEST(execution_posix, test_lifecycle) {
    test_lifecycle_and_attributes();
}
ZTEST(execution_posix, test_mutex) {
    test_mutex_contention();
}
ZTEST(execution_posix, test_semaphore) {
    test_semaphore_timeout();
}
ZTEST(execution_posix, test_message_exchange) {
    test_messages();
}
ZTEST(execution_posix, test_periodic) {
    test_periodic_monotonic();
}
ZTEST_SUITE(execution_posix, NULL, NULL, NULL, NULL, NULL);
#else
int main(void) {
    test_lifecycle_and_attributes();
    test_mutex_contention();
    test_semaphore_timeout();
    test_messages();
    test_periodic_monotonic();
    puts("POSIX probes: 5 passed");
    return 0;
}
#endif
