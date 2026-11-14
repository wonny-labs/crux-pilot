#include "sync_internal.h"
#include "thread_internal.h"
#include <zephyr/kernel.h>

typedef struct {
    struct k_sem start;
    struct k_sem done;
    crux_semaphore_t semaphore;
    uint32_t timeout_ms;
    crux_status_t result;
} metadata_wait_t;

static void metadata_wait_worker(void *argument)
{
    metadata_wait_t *wait = argument;
    (void)k_sem_take(&wait->start, K_FOREVER);
    wait->result = crux_semaphore_take(wait->semaphore, wait->timeout_ms);
    k_sem_give(&wait->done);
}

typedef struct {
    struct k_sem start;
    struct k_sem done;
    crux_thread_t target;
    crux_status_t result;
} metadata_join_t;

static void finished_target(void *unused)
{
    (void)unused;
}
static void metadata_join_worker(void *argument)
{
    metadata_join_t *wait = argument;
    (void)k_sem_take(&wait->start, K_FOREVER);
    wait->result = crux_thread_join(&wait->target, 100);
    k_sem_give(&wait->done);
}
static int thread_metadata_admission_case(void)
{
    const crux_thread_options_t options = {.priority = CRUX_THREAD_PRIORITY_NORMAL};
    metadata_join_t wait = {0};
    k_sem_init(&wait.start, 0, 1);
    k_sem_init(&wait.done, 0, 1);
    crux_thread_t caller;
    if (crux_thread_create(&options, finished_target, NULL, &wait.target) != CRUX_OK ||
        crux_thread_create(&options, metadata_join_worker, &wait, &caller) != CRUX_OK) {
        return 4;
    }
    thread_native_metadata_lock();
    k_sem_give(&wait.start);
    bool finished = k_sem_take(&wait.done, K_MSEC(500)) == 0;
    thread_native_metadata_unlock();
    if (crux_thread_join(&caller, 1000) != CRUX_OK || !finished ||
        wait.result != CRUX_ERR_TIMEOUT || crux_thread_join(&wait.target, 1000) != CRUX_OK) {
        return 5;
    }
    return 0;
}

/* Sync admission takes no lock. A create paused inside the provider, where a global metadata
 * mutex used to be held, must not delay a poll or a finite wait on other objects. */
static struct k_sem create_paused;
static struct k_sem release_create;
static bool pause_create;

crux_status_t __real_crux_semaphore_impl_create(sync_native_semaphore_t *native, uint32_t initial,
                                                uint32_t maximum);
crux_status_t __wrap_crux_semaphore_impl_create(sync_native_semaphore_t *native, uint32_t initial,
                                                uint32_t maximum)
{
    if (pause_create) {
        pause_create = false;
        k_sem_give(&create_paused);
        (void)k_sem_take(&release_create, K_FOREVER);
    }
    return __real_crux_semaphore_impl_create(native, initial, maximum);
}

static void paused_create_worker(void *argument)
{
    crux_status_t *result = argument;
    crux_semaphore_t created;
    *result = crux_semaphore_create(0, 1, &created);
    if (*result == CRUX_OK) {
        *result = crux_semaphore_destroy(&created);
    }
}

int execution_metadata_admission_case(void)
{
    const crux_thread_options_t options = {.priority = CRUX_THREAD_PRIORITY_NORMAL};
    metadata_wait_t poll = {.timeout_ms = 0};
    metadata_wait_t finite = {.timeout_ms = 100};
    k_sem_init(&poll.start, 0, 1);
    k_sem_init(&poll.done, 0, 1);
    k_sem_init(&finite.start, 0, 1);
    k_sem_init(&finite.done, 0, 1);
    k_sem_init(&create_paused, 0, 1);
    k_sem_init(&release_create, 0, 1);
    if (crux_semaphore_create(1, 1, &poll.semaphore) != CRUX_OK ||
        crux_semaphore_create(1, 1, &finite.semaphore) != CRUX_OK) {
        return 1;
    }
    crux_thread_t poller;
    crux_thread_t waiter;
    crux_thread_t creator;
    crux_status_t create_result = CRUX_ERR_IO;
    pause_create = true;
    if (crux_thread_create(&options, metadata_wait_worker, &poll, &poller) != CRUX_OK ||
        crux_thread_create(&options, metadata_wait_worker, &finite, &waiter) != CRUX_OK ||
        crux_thread_create(&options, paused_create_worker, &create_result, &creator) != CRUX_OK) {
        return 2;
    }
    if (k_sem_take(&create_paused, K_MSEC(500)) != 0) {
        return 3;
    }

    /* Both workers attempt admission while the create stays inside the provider. */
    k_sem_give(&poll.start);
    k_sem_give(&finite.start);
    bool poll_finished = k_sem_take(&poll.done, K_MSEC(500)) == 0;
    bool finite_finished = k_sem_take(&finite.done, K_MSEC(500)) == 0;
    k_sem_give(&release_create);

    crux_status_t poll_join = crux_thread_join(&poller, 1000);
    crux_status_t finite_join = crux_thread_join(&waiter, 1000);
    crux_status_t create_join = crux_thread_join(&creator, 1000);
    if (!poll_finished || poll_join != CRUX_OK || poll.result != CRUX_OK || !finite_finished ||
        finite_join != CRUX_OK || finite.result != CRUX_OK || create_join != CRUX_OK ||
        create_result != CRUX_OK || crux_semaphore_take(poll.semaphore, 0) != CRUX_ERR_BUSY ||
        crux_semaphore_take(finite.semaphore, 0) != CRUX_ERR_BUSY ||
        crux_semaphore_destroy(&poll.semaphore) != CRUX_OK ||
        crux_semaphore_destroy(&finite.semaphore) != CRUX_OK) {
        return 4;
    }
    return thread_metadata_admission_case();
}
