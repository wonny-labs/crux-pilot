#include "sync_internal.h"
#include "thread_internal.h"
#include <pthread.h>
#include <semaphore.h>
#include <stdatomic.h>
#include <stdio.h>
#include <time.h>

static atomic_bool capture_holder;
static bool hold_thread_metadata;
static sem_t metadata_locked;
static sem_t release_metadata;
static sem_t started;
static sem_t completed;
static sem_t worker_gate;
static crux_thread_t worker;
static crux_mutex_t mutex;
static crux_semaphore_t available;
static crux_semaphore_t empty;

/* GNU ld --wrap requires these reserved linker symbol names. */
// NOLINTBEGIN(bugprone-reserved-identifier)
void __real_sync_native_metadata_lock(void);
void __real_thread_native_metadata_lock(void);
void sync_native_metadata_unlock(void);
static void capture_metadata(void)
{
    if (atomic_exchange(&capture_holder, false)) {
        (void)sem_post(&metadata_locked);
        (void)sem_wait(&release_metadata);
    }
}
void __wrap_sync_native_metadata_lock(void)
{
    __real_sync_native_metadata_lock();
    capture_metadata();
}
void __wrap_thread_native_metadata_lock(void)
{
    __real_thread_native_metadata_lock();
    capture_metadata();
}
// NOLINTEND(bugprone-reserved-identifier)

static void *hold_metadata(void *unused)
{
    (void)unused;
    atomic_store(&capture_holder, true);
    if (hold_thread_metadata) {
        __wrap_thread_native_metadata_lock();
        thread_native_metadata_unlock();
    } else {
        __wrap_sync_native_metadata_lock();
        sync_native_metadata_unlock();
    }
    return NULL;
}

typedef enum { CALL_POLL, CALL_MUTEX, CALL_SEMAPHORE, CALL_JOIN } call_kind_t;

typedef struct {
    call_kind_t kind;
    crux_status_t result;
} call_t;

static void *invoke_wait(void *argument)
{
    call_t *call = argument;
    (void)sem_post(&started);
    switch (call->kind) {
    case CALL_POLL:
        call->result = crux_semaphore_take(available, 0);
        break;
    case CALL_MUTEX:
        call->result = crux_mutex_lock(mutex, 100);
        if (call->result == CRUX_OK) {
            (void)crux_mutex_unlock(mutex);
        }
        break;
    case CALL_SEMAPHORE:
        call->result = crux_semaphore_take(empty, 100);
        break;
    case CALL_JOIN:
        call->result = crux_thread_join(&worker, 100);
        break;
    }
    (void)sem_post(&completed);
    return NULL;
}

static void wait_worker(void *unused)
{
    (void)unused;
    (void)sem_wait(&worker_gate);
}

static int wait_for(sem_t *semaphore, long milliseconds)
{
    struct timespec deadline;
    if (clock_gettime(CLOCK_REALTIME, &deadline) != 0) {
        return -1;
    }
    deadline.tv_nsec += milliseconds * 1000000L;
    deadline.tv_sec += deadline.tv_nsec / 1000000000L;
    deadline.tv_nsec %= 1000000000L;
    return sem_timedwait(semaphore, &deadline);
}

static int run_wait(call_t *call, pthread_t *thread, int *returned_while_held)
{
    if (pthread_create(thread, NULL, invoke_wait, call) != 0 || wait_for(&started, 1000) != 0) {
        return -1;
    }
    *returned_while_held = wait_for(&completed, 500) == 0;
    return 0;
}

int main(void)
{
    if (sem_init(&metadata_locked, 0, 0) != 0 || sem_init(&release_metadata, 0, 0) != 0 ||
        sem_init(&started, 0, 0) != 0 || sem_init(&completed, 0, 0) != 0 ||
        sem_init(&worker_gate, 0, 0) != 0) {
        return 1;
    }
    if (crux_mutex_create(&mutex) != CRUX_OK ||
        crux_semaphore_create(1, 1, &available) != CRUX_OK ||
        crux_semaphore_create(0, 1, &empty) != CRUX_OK) {
        return 2;
    }
    const crux_thread_options_t options = {.priority = CRUX_THREAD_PRIORITY_NORMAL};
    if (crux_thread_create(&options, wait_worker, NULL, &worker) != CRUX_OK) {
        return 3;
    }

    call_t calls[] = {
        {.kind = CALL_POLL}, {.kind = CALL_MUTEX}, {.kind = CALL_SEMAPHORE}, {.kind = CALL_JOIN}};
    const crux_status_t expected[] = {CRUX_ERR_BUSY, CRUX_ERR_TIMEOUT, CRUX_ERR_TIMEOUT,
                                      CRUX_ERR_TIMEOUT};
    int returned_while_held[sizeof(calls) / sizeof(calls[0])] = {0};
    int setup_error = 0;
    for (size_t i = 0; i < sizeof(calls) / sizeof(calls[0]); ++i) {
        pthread_t holder;
        pthread_t caller;
        hold_thread_metadata = calls[i].kind == CALL_JOIN;
        if (pthread_create(&holder, NULL, hold_metadata, NULL) != 0 ||
            sem_wait(&metadata_locked) != 0) {
            return 4;
        }
        if (run_wait(&calls[i], &caller, &returned_while_held[i]) != 0) {
            return 5;
        }
        (void)sem_post(&release_metadata);
        if (pthread_join(holder, NULL) != 0 || pthread_join(caller, NULL) != 0) {
            setup_error = 1;
        }
    }
    (void)sem_post(&worker_gate);
    if (crux_thread_join(&worker, 1000) != CRUX_OK) {
        setup_error = 1;
    }
    for (size_t i = 0; i < sizeof(calls) / sizeof(calls[0]); ++i) {
        if (!returned_while_held[i] || calls[i].result != expected[i]) {
            fprintf(stderr, "metadata wait case %zu returned=%d while-held=%d expected=%d\n", i,
                    calls[i].result, returned_while_held[i], expected[i]);
            setup_error = 1;
        }
    }
    if (crux_mutex_destroy(&mutex) != CRUX_OK || crux_semaphore_destroy(&available) != CRUX_OK ||
        crux_semaphore_destroy(&empty) != CRUX_OK) {
        setup_error = 1;
    }
    (void)sem_destroy(&metadata_locked);
    (void)sem_destroy(&release_metadata);
    (void)sem_destroy(&started);
    (void)sem_destroy(&completed);
    (void)sem_destroy(&worker_gate);
    return setup_error;
}
