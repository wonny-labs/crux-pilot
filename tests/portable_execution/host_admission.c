#include "sync_internal.h"
#include "thread_internal.h"
#include <pthread.h>
#include <semaphore.h>
#include <stdatomic.h>
#include <stdio.h>
#include <time.h>

/* Thread admission waits under its metadata lock. Sync admission takes no lock, so a Sync
 * create paused inside the provider, where a global metadata lock used to be held, must not
 * delay operations on other objects. */
typedef enum { HOLD_THREAD_METADATA, HOLD_MUTEX_CREATE, HOLD_SEMAPHORE_CREATE } hold_kind_t;

static atomic_bool capture_holder;
static hold_kind_t hold_kind;
static crux_status_t holder_result;
static sem_t holder_paused;
static sem_t release_holder;
static sem_t started;
static sem_t completed;
static sem_t worker_gate;
static crux_thread_t worker;
static crux_mutex_t mutex;
static crux_semaphore_t available;

/* GNU ld --wrap requires these reserved linker symbol names. */
// NOLINTBEGIN(bugprone-reserved-identifier)
void __real_thread_native_metadata_lock(void);
crux_status_t __real_crux_mutex_impl_create(sync_native_mutex_t *native);
crux_status_t __real_crux_semaphore_impl_create(sync_native_semaphore_t *native, uint32_t initial,
                                                uint32_t maximum);
static void capture_holder_here(void)
{
    if (atomic_exchange(&capture_holder, false)) {
        (void)sem_post(&holder_paused);
        (void)sem_wait(&release_holder);
    }
}
void __wrap_thread_native_metadata_lock(void)
{
    __real_thread_native_metadata_lock();
    capture_holder_here();
}
crux_status_t __wrap_crux_mutex_impl_create(sync_native_mutex_t *native)
{
    capture_holder_here();
    return __real_crux_mutex_impl_create(native);
}
/* Keep the public initial/maximum order at the native boundary. */
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
crux_status_t __wrap_crux_semaphore_impl_create(sync_native_semaphore_t *native, uint32_t initial,
                                                uint32_t maximum)
{
    capture_holder_here();
    return __real_crux_semaphore_impl_create(native, initial, maximum);
}
// NOLINTEND(bugprone-reserved-identifier)

static void *hold(void *unused)
{
    (void)unused;
    atomic_store(&capture_holder, true);
    switch (hold_kind) {
    case HOLD_THREAD_METADATA:
        __wrap_thread_native_metadata_lock();
        thread_native_metadata_unlock();
        holder_result = CRUX_OK;
        break;
    case HOLD_MUTEX_CREATE: {
        crux_mutex_t created;
        holder_result = crux_mutex_create(&created);
        if (holder_result == CRUX_OK) {
            holder_result = crux_mutex_destroy(&created);
        }
        break;
    }
    case HOLD_SEMAPHORE_CREATE: {
        crux_semaphore_t created;
        holder_result = crux_semaphore_create(0, 1, &created);
        if (holder_result == CRUX_OK) {
            holder_result = crux_semaphore_destroy(&created);
        }
        break;
    }
    }
    return NULL;
}

typedef enum { CALL_POLL, CALL_MUTEX, CALL_SEMAPHORE, CALL_GIVE, CALL_JOIN } call_kind_t;

typedef struct {
    call_kind_t kind;
    hold_kind_t hold;
    crux_status_t expected;
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
            call->result = crux_mutex_unlock(mutex);
        }
        break;
    case CALL_SEMAPHORE:
        call->result = crux_semaphore_take(available, 100);
        break;
    case CALL_GIVE:
        call->result = crux_semaphore_give(available);
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
    if (sem_init(&holder_paused, 0, 0) != 0 || sem_init(&release_holder, 0, 0) != 0 ||
        sem_init(&started, 0, 0) != 0 || sem_init(&completed, 0, 0) != 0 ||
        sem_init(&worker_gate, 0, 0) != 0) {
        return 1;
    }
    if (crux_mutex_create(&mutex) != CRUX_OK ||
        crux_semaphore_create(2, 2, &available) != CRUX_OK) {
        return 2;
    }
    const crux_thread_options_t options = {.priority = CRUX_THREAD_PRIORITY_NORMAL};
    if (crux_thread_create(&options, wait_worker, NULL, &worker) != CRUX_OK) {
        return 3;
    }

    /* The poll and the finite take each consume one of the two tokens; the give restores one. */
    call_t calls[] = {
        {.kind = CALL_POLL, .hold = HOLD_SEMAPHORE_CREATE, .expected = CRUX_OK},
        {.kind = CALL_MUTEX, .hold = HOLD_MUTEX_CREATE, .expected = CRUX_OK},
        {.kind = CALL_SEMAPHORE, .hold = HOLD_SEMAPHORE_CREATE, .expected = CRUX_OK},
        {.kind = CALL_GIVE, .hold = HOLD_SEMAPHORE_CREATE, .expected = CRUX_OK},
        {.kind = CALL_JOIN, .hold = HOLD_THREAD_METADATA, .expected = CRUX_ERR_TIMEOUT},
    };
    int returned_while_held[sizeof(calls) / sizeof(calls[0])] = {0};
    int setup_error = 0;
    for (size_t i = 0; i < sizeof(calls) / sizeof(calls[0]); ++i) {
        pthread_t holder;
        pthread_t caller;
        hold_kind = calls[i].hold;
        holder_result = CRUX_ERR_IO;
        if (pthread_create(&holder, NULL, hold, NULL) != 0 || sem_wait(&holder_paused) != 0) {
            return 4;
        }
        if (run_wait(&calls[i], &caller, &returned_while_held[i]) != 0) {
            return 5;
        }
        (void)sem_post(&release_holder);
        if (pthread_join(holder, NULL) != 0 || pthread_join(caller, NULL) != 0 ||
            holder_result != CRUX_OK) {
            setup_error = 1;
        }
    }
    (void)sem_post(&worker_gate);
    if (crux_thread_join(&worker, 1000) != CRUX_OK) {
        setup_error = 1;
    }
    for (size_t i = 0; i < sizeof(calls) / sizeof(calls[0]); ++i) {
        if (!returned_while_held[i] || calls[i].result != calls[i].expected) {
            fprintf(stderr, "admission case %zu returned=%d while-held=%d expected=%d\n", i,
                    calls[i].result, returned_while_held[i], calls[i].expected);
            setup_error = 1;
        }
    }
    if (crux_semaphore_take(available, 0) != CRUX_OK ||
        crux_semaphore_take(available, 0) != CRUX_ERR_BUSY) {
        setup_error = 1;
    }
    if (crux_mutex_destroy(&mutex) != CRUX_OK || crux_semaphore_destroy(&available) != CRUX_OK) {
        setup_error = 1;
    }
    (void)sem_destroy(&holder_paused);
    (void)sem_destroy(&release_holder);
    (void)sem_destroy(&started);
    (void)sem_destroy(&completed);
    (void)sem_destroy(&worker_gate);
    return setup_error;
}
