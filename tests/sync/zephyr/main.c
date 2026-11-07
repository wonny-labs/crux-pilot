/* Native integration probe: Sync must not disturb the kernel's priority inheritance. */
#include "sync_internal.h"
#include <crux/sync.h>
#include <zephyr/kernel.h>
#include <zephyr/ztest.h>

#define HOLDER_PRIORITY 7
#define WAITER_PRIORITY 2
#define STACK_BYTES 2048
#define STRESS_THREADS 3
#define STRESS_ROUNDS 100

K_THREAD_STACK_ARRAY_DEFINE(stacks, STRESS_THREADS, STACK_BYTES);
static struct k_thread threads[STRESS_THREADS];
static crux_mutex_t shared_mutex;
static crux_semaphore_t shared_semaphore;
static crux_status_t waiter_result;
static atomic_t waiter_done;
static bool started[STRESS_THREADS];
static crux_status_t early_create_result;

static int own_priority(void)
{
    return k_thread_priority_get(k_current_get());
}

static void lock_then_unlock(void *timeout_ms, void *unused2, void *unused3)
{
    (void)unused2;
    (void)unused3;
    waiter_result = crux_mutex_lock(shared_mutex, (uint32_t)(uintptr_t)timeout_ms);
    if (waiter_result == CRUX_OK) {
        waiter_result = crux_mutex_unlock(shared_mutex);
    }
    atomic_set(&waiter_done, 1);
}

static void lock_sleep_unlock(void *timeout_ms, void *unused2, void *unused3)
{
    (void)unused2;
    (void)unused3;
    waiter_result = crux_mutex_lock(shared_mutex, (uint32_t)(uintptr_t)timeout_ms);
    if (waiter_result == CRUX_OK) {
        k_sleep(K_MSEC(5));
        waiter_result = crux_mutex_unlock(shared_mutex);
    }
    atomic_set(&waiter_done, 1);
}

static void take_semaphore(void *unused1, void *unused2, void *unused3)
{
    (void)unused1;
    (void)unused2;
    (void)unused3;
    waiter_result = crux_semaphore_take(shared_semaphore, 1000);
    atomic_set(&waiter_done, 1);
}

/* The new thread has the higher priority, so it runs at once and blocks. */
static void start_waiter(k_thread_entry_t entry, uint32_t timeout_ms)
{
    atomic_set(&waiter_done, 0);
    waiter_result = CRUX_ERR_IO;
    (void)k_thread_create(&threads[0], stacks[0], STACK_BYTES, entry, (void *)(uintptr_t)timeout_ms,
                          NULL, NULL, WAITER_PRIORITY, 0, K_NO_WAIT);
    started[0] = true;
}

static void hold_shared_mutex(void)
{
    k_thread_priority_set(k_current_get(), HOLDER_PRIORITY);
    zassert_equal(crux_mutex_create(&shared_mutex), CRUX_OK);
    zassert_equal(crux_mutex_lock(shared_mutex, 1000), CRUX_OK);
}

static void release_shared_mutex(void)
{
    zassert_equal(k_thread_join(&threads[0], K_SECONDS(5)), 0);
    zassert_equal(crux_mutex_destroy(&shared_mutex), CRUX_OK);
}

ZTEST(sync_priority, test_holder_returns_to_its_priority_after_unlock)
{
    hold_shared_mutex();
    start_waiter(lock_then_unlock, 1000);
    zassert_equal(own_priority(), WAITER_PRIORITY, "the waiter raises the holder");
    zassert_equal(crux_mutex_unlock(shared_mutex), CRUX_OK);
    zassert_equal(own_priority(), HOLDER_PRIORITY, "the holder keeps the inherited priority");
    zassert_equal(waiter_result, CRUX_OK);
    release_shared_mutex();
}

ZTEST(sync_priority, test_holder_returns_to_its_priority_when_the_waiter_times_out)
{
    hold_shared_mutex();
    start_waiter(lock_then_unlock, 20);
    zassert_equal(own_priority(), WAITER_PRIORITY);
    k_sleep(K_MSEC(100));
    zassert_equal(atomic_get(&waiter_done), 1);
    zassert_equal(waiter_result, CRUX_ERR_TIMEOUT);
    zassert_equal(own_priority(), HOLDER_PRIORITY, "still holding, with no waiter left");
    zassert_equal(crux_mutex_unlock(shared_mutex), CRUX_OK);
    zassert_equal(own_priority(), HOLDER_PRIORITY);
    release_shared_mutex();
}

ZTEST(sync_priority, test_inherited_priority_survives_other_sync_calls)
{
    crux_mutex_t other;
    crux_semaphore_t semaphore;

    hold_shared_mutex();
    start_waiter(lock_then_unlock, 1000);
    zassert_equal(crux_mutex_create(&other), CRUX_OK);
    zassert_equal(crux_mutex_lock(other, 0), CRUX_OK);
    zassert_equal(crux_mutex_unlock(other), CRUX_OK);
    zassert_equal(crux_mutex_destroy(&other), CRUX_OK);
    zassert_equal(crux_semaphore_create(1, 1, &semaphore), CRUX_OK);
    zassert_equal(crux_semaphore_take(semaphore, 0), CRUX_OK);
    zassert_equal(crux_semaphore_give(semaphore), CRUX_OK);
    zassert_equal(crux_semaphore_destroy(&semaphore), CRUX_OK);
    zassert_equal(own_priority(), WAITER_PRIORITY, "the mutex is still held and waited for");
    zassert_equal(crux_mutex_unlock(shared_mutex), CRUX_OK);
    zassert_equal(own_priority(), HOLDER_PRIORITY);
    release_shared_mutex();
}

ZTEST(sync_priority, test_refused_poll_leaves_the_priority_alone)
{
    hold_shared_mutex();
    start_waiter(lock_then_unlock, 1000);
    zassert_equal(crux_mutex_lock(shared_mutex, 0), CRUX_ERR_INVALID_STATE);
    zassert_equal(own_priority(), WAITER_PRIORITY);
    zassert_equal(crux_mutex_unlock(shared_mutex), CRUX_OK);
    zassert_equal(own_priority(), HOLDER_PRIORITY);
    release_shared_mutex();
}

ZTEST(sync_priority, test_unlock_lets_the_higher_waiter_run_before_it_returns)
{
    hold_shared_mutex();
    start_waiter(lock_then_unlock, 1000);
    zassert_equal(crux_mutex_unlock(shared_mutex), CRUX_OK);
    zassert_equal(atomic_get(&waiter_done), 1, "the wake-up was deferred past the unlock");
    release_shared_mutex();
}

ZTEST(sync_priority, test_new_owner_keeps_the_mutex_after_the_old_owner_resumes)
{
    hold_shared_mutex();
    start_waiter(lock_sleep_unlock, 1000);
    zassert_equal(crux_mutex_unlock(shared_mutex), CRUX_OK);
    zassert_equal(atomic_get(&waiter_done), 0, "the new owner is asleep, holding the mutex");
    zassert_equal(crux_mutex_lock(shared_mutex, 0), CRUX_ERR_BUSY);
    zassert_equal(crux_mutex_destroy(&shared_mutex), CRUX_ERR_BUSY);
    zassert_equal(k_thread_join(&threads[0], K_SECONDS(5)), 0);
    zassert_equal(waiter_result, CRUX_OK, "the new owner's unlock was refused");
    zassert_equal(crux_mutex_lock(shared_mutex, 0), CRUX_OK);
    zassert_equal(crux_mutex_unlock(shared_mutex), CRUX_OK);
    zassert_equal(crux_mutex_destroy(&shared_mutex), CRUX_OK);
}

/* A target interrupt can switch threads right after a native unlock that lowers the caller,
 * which native_sim never does inside a section. The wrapper stands in for that switch. */
crux_status_t __real_crux_mutex_impl_unlock(sync_native_mutex_t *mutex);
static bool switch_after_native_unlock;
crux_status_t __wrap_crux_mutex_impl_unlock(sync_native_mutex_t *mutex)
{
    crux_status_t result = __real_crux_mutex_impl_unlock(mutex);
    if (switch_after_native_unlock) {
        switch_after_native_unlock = false;
        k_sleep(K_MSEC(1));
    }
    return result;
}

ZTEST(sync_priority, test_switch_after_the_native_unlock_cannot_corrupt_the_slot)
{
    hold_shared_mutex();
    start_waiter(lock_sleep_unlock, 1000);
    switch_after_native_unlock = true;
    zassert_equal(crux_mutex_unlock(shared_mutex), CRUX_OK);
    zassert_false(switch_after_native_unlock, "the native unlock did not run");
    zassert_equal(k_thread_join(&threads[0], K_SECONDS(5)), 0);
    zassert_equal(waiter_result, CRUX_OK, "the new owner's unlock was refused");
    zassert_equal(crux_mutex_destroy(&shared_mutex), CRUX_OK);
}

ZTEST(sync_priority, test_give_lets_the_higher_taker_run_before_it_returns)
{
    k_thread_priority_set(k_current_get(), HOLDER_PRIORITY);
    zassert_equal(crux_semaphore_create(0, 1, &shared_semaphore), CRUX_OK);
    start_waiter(take_semaphore, 0);
    zassert_equal(crux_semaphore_give(shared_semaphore), CRUX_OK);
    zassert_equal(atomic_get(&waiter_done), 1, "the wake-up was deferred past the give");
    zassert_equal(waiter_result, CRUX_OK);
    zassert_equal(k_thread_join(&threads[0], K_SECONDS(5)), 0);
    zassert_equal(crux_semaphore_destroy(&shared_semaphore), CRUX_OK);
}

static uint32_t stress_count;
static atomic_t stress_errors;

static void stress_worker(void *priority_value, void *unused2, void *unused3)
{
    (void)unused2;
    (void)unused3;
    const int priority = (int)(intptr_t)priority_value;

    for (int round = 0; round < STRESS_ROUNDS; ++round) {
        if (crux_mutex_lock(shared_mutex, 5000) != CRUX_OK) {
            atomic_inc(&stress_errors);
            return;
        }
        const uint32_t seen = stress_count;
        /* Sleeping here lets other workers run: they block on the mutex and raise this
         * thread, and a second owner would overwrite the count. */
        k_sleep(K_TICKS(2));
        stress_count = seen + 1;
        if (crux_mutex_unlock(shared_mutex) != CRUX_OK || own_priority() != priority) {
            atomic_inc(&stress_errors);
        }
        k_sleep(K_TICKS(1));
    }
}

ZTEST(sync_priority, test_contending_priorities_keep_exclusion_and_own_priority)
{
    static const int priorities[STRESS_THREADS] = {3, 5, 7};

    stress_count = 0;
    atomic_set(&stress_errors, 0);
    zassert_equal(crux_mutex_create(&shared_mutex), CRUX_OK);
    for (int i = 0; i < STRESS_THREADS; ++i) {
        (void)k_thread_create(&threads[i], stacks[i], STACK_BYTES, stress_worker,
                              (void *)(intptr_t)priorities[i], NULL, NULL, priorities[i], 0,
                              K_NO_WAIT);
        started[i] = true;
    }
    for (int i = 0; i < STRESS_THREADS; ++i) {
        zassert_equal(k_thread_join(&threads[i], K_SECONDS(60)), 0);
    }
    zassert_equal(atomic_get(&stress_errors), 0);
    zassert_equal(stress_count, STRESS_THREADS * STRESS_ROUNDS);
    zassert_equal(crux_mutex_destroy(&shared_mutex), CRUX_OK);
}

ZTEST(sync_slots, test_every_configured_mutex_slot_is_usable)
{
    static crux_mutex_t mutexes[CONFIG_CRUX_SYNC_MUTEX_COUNT];
    crux_mutex_t extra;

    for (int i = 0; i < CONFIG_CRUX_SYNC_MUTEX_COUNT; ++i) {
        zassert_equal(crux_mutex_create(&mutexes[i]), CRUX_OK, "slot %d", i);
    }
    zassert_equal(crux_mutex_create(&extra), CRUX_ERR_NO_RESOURCES);
    for (int i = 0; i < CONFIG_CRUX_SYNC_MUTEX_COUNT; ++i) {
        zassert_equal(crux_mutex_lock(mutexes[i], 0), CRUX_OK, "slot %d", i);
    }
    for (int i = CONFIG_CRUX_SYNC_MUTEX_COUNT - 1; i >= 0; --i) {
        zassert_equal(crux_mutex_unlock(mutexes[i]), CRUX_OK, "slot %d", i);
    }
    const crux_mutex_t last = mutexes[CONFIG_CRUX_SYNC_MUTEX_COUNT - 1];
    for (int i = 0; i < CONFIG_CRUX_SYNC_MUTEX_COUNT; ++i) {
        zassert_equal(crux_mutex_destroy(&mutexes[i]), CRUX_OK, "slot %d", i);
    }
    zassert_equal(crux_mutex_lock(last, 0), CRUX_ERR_INVALID_ARGUMENT, "stale handle");
}

static int create_before_the_kernel_runs(void)
{
    crux_mutex_t mutex;
    early_create_result = crux_mutex_create(&mutex);
    return 0;
}
SYS_INIT(create_before_the_kernel_runs, PRE_KERNEL_2, 0);

ZTEST(sync_slots, test_pre_kernel_calls_are_refused)
{
    zassert_equal(early_create_result, CRUX_ERR_NOT_SUPPORTED);
}

/* A failed assertion leaves workers behind, which would disturb the next test. */
static void stop_workers(void *fixture)
{
    (void)fixture;
    for (int i = 0; i < STRESS_THREADS; ++i) {
        if (started[i]) {
            k_thread_abort(&threads[i]);
            started[i] = false;
        }
    }
}

ZTEST_SUITE(sync_priority, NULL, NULL, NULL, stop_workers, NULL);
ZTEST_SUITE(sync_slots, NULL, NULL, NULL, NULL, NULL);
