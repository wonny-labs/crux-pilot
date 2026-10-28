#include "sync_internal.h"
#include "thread_internal.h"
#include <pthread.h>
#include <semaphore.h>
#include <stdatomic.h>
#include <stdio.h>
#include <time.h>

static atomic_bool native_completed;
static sem_t post_lock_attempted;
static sem_t release_post_lock;
static sem_t completed;
static crux_semaphore_t semaphore;
static crux_mutex_t mutex;
static crux_thread_t target;
static crux_status_t result;

/* GNU ld --wrap requires these reserved linker symbol names. */
// NOLINTBEGIN(bugprone-reserved-identifier)
void __real_sync_native_metadata_lock(void);
void __real_thread_native_metadata_lock(void);
crux_status_t __real_crux_mutex_impl_lock(sync_native_mutex_t *native,
                                            const sync_native_deadline_t *deadline);
crux_status_t __real_crux_semaphore_impl_take(sync_native_semaphore_t *native,
                                                const sync_native_deadline_t *deadline);
crux_status_t __real_crux_thread_impl_join(thread_native_thread_t *native,
                                               const thread_native_deadline_t *deadline);

static void pause_post_lock(void)
{
    if (atomic_exchange(&native_completed, false)) {
        (void)sem_post(&post_lock_attempted);
        (void)sem_wait(&release_post_lock);
    }
}
void __wrap_sync_native_metadata_lock(void)
{
    pause_post_lock();
    __real_sync_native_metadata_lock();
}
void __wrap_thread_native_metadata_lock(void)
{
    pause_post_lock();
    __real_thread_native_metadata_lock();
}

crux_status_t __wrap_crux_mutex_impl_lock(sync_native_mutex_t *native,
                                            const sync_native_deadline_t *deadline)
{
    crux_status_t status = __real_crux_mutex_impl_lock(native, deadline);
    if (deadline->poll) {
        atomic_store(&native_completed, true);
    }
    return status;
}

crux_status_t __wrap_crux_semaphore_impl_take(sync_native_semaphore_t *native,
                                                const sync_native_deadline_t *deadline)
{
    crux_status_t status = __real_crux_semaphore_impl_take(native, deadline);
    if (deadline->poll) {
        atomic_store(&native_completed, true);
    }
    return status;
}

crux_status_t __wrap_crux_thread_impl_join(thread_native_thread_t *native,
                                               const thread_native_deadline_t *deadline)
{
    crux_status_t status = __real_crux_thread_impl_join(native, deadline);
    if (deadline->poll) {
        atomic_store(&native_completed, true);
    }
    return status;
}
// NOLINTEND(bugprone-reserved-identifier)

static int wait_for(sem_t *event, long milliseconds)
{
    struct timespec deadline;
    if (clock_gettime(CLOCK_REALTIME, &deadline) != 0) {
        return -1;
    }
    deadline.tv_nsec += milliseconds * 1000000L;
    deadline.tv_sec += deadline.tv_nsec / 1000000000L;
    deadline.tv_nsec %= 1000000000L;
    return sem_timedwait(event, &deadline);
}

typedef enum { POLL_SEMAPHORE, POLL_MUTEX, POLL_JOIN } poll_kind_t;

static void *poll_operation(void *argument)
{
    poll_kind_t kind = *(poll_kind_t *)argument;
    switch (kind) {
    case POLL_SEMAPHORE:
        result = crux_semaphore_take(semaphore, 0);
        break;
    case POLL_MUTEX:
        result = crux_mutex_lock(mutex, 0);
        atomic_store(&native_completed, false);
        if (result == CRUX_OK) {
            (void)crux_mutex_unlock(mutex);
        }
        break;
    case POLL_JOIN:
        result = crux_thread_join(&target, 0);
        break;
    }
    atomic_store(&native_completed, false);
    (void)sem_post(&completed);
    return NULL;
}

static void finished_worker(void *unused)
{
    (void)unused;
}

static int run_poll(poll_kind_t kind)
{
    pthread_t caller;
    atomic_store(&native_completed, false);
    if (pthread_create(&caller, NULL, poll_operation, &kind) != 0) {
        return 1;
    }
    int reacquired = wait_for(&post_lock_attempted, 100) == 0;
    int returned_without_release = wait_for(&completed, 500) == 0;
    (void)sem_post(&release_post_lock);
    if (pthread_join(caller, NULL) != 0) {
        return 2;
    }
    if (reacquired || !returned_without_release || result != CRUX_OK) {
        fprintf(stderr, "poll kind %d status=%d post-lock=%d completed=%d\n", kind, result,
                reacquired, returned_without_release);
        return 3;
    }
    return 0;
}

int main(void)
{
    if (sem_init(&post_lock_attempted, 0, 0) != 0 || sem_init(&release_post_lock, 0, 0) != 0 ||
        sem_init(&completed, 0, 0) != 0 || crux_semaphore_create(1, 1, &semaphore) != CRUX_OK ||
        crux_mutex_create(&mutex) != CRUX_OK) {
        return 1;
    }
    const crux_thread_options_t options = {.priority = CRUX_THREAD_PRIORITY_NORMAL};
    if (crux_thread_create(&options, finished_worker, NULL, &target) != CRUX_OK) {
        return 2;
    }
    struct timespec pause = {.tv_nsec = 20000000L};
    (void)nanosleep(&pause, NULL);

    int status = run_poll(POLL_SEMAPHORE);
    if (status == 0 && crux_semaphore_take(semaphore, 0) != CRUX_ERR_BUSY) {
        status = 4;
    }
    if (status == 0) {
        status = run_poll(POLL_MUTEX);
    }
    if (status == 0) {
        status = run_poll(POLL_JOIN);
    }
    if ((target.slot != 0 && crux_thread_join(&target, 1000) != CRUX_OK) ||
        crux_mutex_destroy(&mutex) != CRUX_OK || crux_semaphore_destroy(&semaphore) != CRUX_OK) {
        status = 5;
    }
    (void)sem_destroy(&post_lock_attempted);
    (void)sem_destroy(&release_post_lock);
    (void)sem_destroy(&completed);
    return status;
}
