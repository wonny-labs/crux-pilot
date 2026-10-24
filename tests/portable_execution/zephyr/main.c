#include <crux/sync.h>
#include <crux/thread.h>
#include <zephyr/irq_offload.h>
#include <zephyr/kernel.h>
#include <zephyr/ztest.h>

int execution_thread_lifetime(void);
int execution_thread_inputs(void);
int execution_thread_capacity(void);
int execution_mutex_ownership(void);
int execution_mutex_wakeup(void);
int execution_semaphore_tokens(void);
int execution_sync_capacity(void);
int execution_join_timeout_retains(void);
int execution_concurrent_give(void);
int execution_admission_cases(void);
int execution_metadata_admission_case(void);
int crux_firmware_run(void);

uint64_t execution_test_now_ns(void) {
    return k_ticks_to_ns_floor64(k_uptime_ticks());
}
ZTEST(portable_execution, test_thread_lifetime) {
    zassert_equal(execution_thread_lifetime(), 0);
}
ZTEST(portable_execution, test_thread_inputs) {
    zassert_equal(execution_thread_inputs(), 0);
}
ZTEST(portable_execution, test_thread_capacity) {
    zassert_equal(execution_thread_capacity(), 0);
}
ZTEST(portable_execution, test_mutex_ownership) {
    zassert_equal(execution_mutex_ownership(), 0);
}
ZTEST(portable_execution, test_mutex_wakeup) {
    zassert_equal(execution_mutex_wakeup(), 0);
}
ZTEST(portable_execution, test_semaphore_tokens) {
    zassert_equal(execution_semaphore_tokens(), 0);
}
ZTEST(portable_execution, test_sync_capacity) {
    zassert_equal(execution_sync_capacity(), 0);
}
ZTEST(portable_execution, test_join_timeout_retains) {
    zassert_equal(execution_join_timeout_retains(), 0);
}
ZTEST(portable_execution, test_concurrent_give) {
    zassert_equal(execution_concurrent_give(), 0);
}
ZTEST(portable_execution, test_admission_lifetimes) {
    zassert_equal(execution_admission_cases(), 0);
}
ZTEST(portable_execution, test_metadata_admission_deadlines) {
    zassert_equal(execution_metadata_admission_case(), 0);
}
ZTEST(portable_execution, test_firmware_consumer) {
    zassert_equal(crux_firmware_run(), 0);
}

static void noop(void *argument) {
    (void)argument;
}
static void reject_isr(const void *argument) {
    (void)argument;
    crux_thread_t thread = {.slot = UINT32_MAX, .generation = UINT32_MAX};
    crux_mutex_t mutex = {.slot = UINT32_MAX, .generation = UINT32_MAX};
    crux_semaphore_t semaphore = {.slot = UINT32_MAX, .generation = UINT32_MAX};
    crux_thread_options_t options = {.priority = CRUX_THREAD_PRIORITY_NORMAL};
    zassert_equal(crux_thread_create(&options, noop, NULL, &thread), CRUX_ERR_NOT_SUPPORTED);
    zassert_equal(thread.slot, 0);
    zassert_equal(thread.generation, 0);
    zassert_equal(crux_thread_join(&thread, 0), CRUX_ERR_NOT_SUPPORTED);
    zassert_equal(crux_mutex_create(&mutex), CRUX_ERR_NOT_SUPPORTED);
    zassert_equal(mutex.slot, 0);
    zassert_equal(mutex.generation, 0);
    zassert_equal(crux_mutex_lock(mutex, 0), CRUX_ERR_NOT_SUPPORTED);
    zassert_equal(crux_mutex_unlock(mutex), CRUX_ERR_NOT_SUPPORTED);
    zassert_equal(crux_mutex_destroy(&mutex), CRUX_ERR_NOT_SUPPORTED);
    zassert_equal(crux_semaphore_create(0, 1, &semaphore), CRUX_ERR_NOT_SUPPORTED);
    zassert_equal(semaphore.slot, 0);
    zassert_equal(semaphore.generation, 0);
    zassert_equal(crux_semaphore_take(semaphore, 0), CRUX_ERR_NOT_SUPPORTED);
    zassert_equal(crux_semaphore_give(semaphore), CRUX_ERR_NOT_SUPPORTED);
    zassert_equal(crux_semaphore_destroy(&semaphore), CRUX_ERR_NOT_SUPPORTED);
}
ZTEST(portable_execution, test_isr_rejected) {
    irq_offload(reject_isr, NULL);
}

typedef struct {
    int priority;
    size_t unused;
    size_t usable;
    int result;
} worker_observation_t;

static void observe_native_worker(void *argument) {
    worker_observation_t *observation = argument;
    observation->priority = k_thread_priority_get(k_current_get());
    observation->result = k_thread_stack_space_get(k_current_get(), &observation->unused);
    observation->usable = k_current_get()->stack_info.size;
}

ZTEST(portable_execution, test_stack_priority_profile) {
    crux_thread_options_t excessive = {.stack_size_bytes = 4097,
                                       .priority = CRUX_THREAD_PRIORITY_NORMAL};
    crux_thread_t rejected;
    zassert_equal(crux_thread_create(&excessive, noop, NULL, &rejected), CRUX_ERR_OUT_OF_RANGE);
    zassert_equal(rejected.slot, 0);
    zassert_equal(rejected.generation, 0);
    for (int priority = CRUX_THREAD_PRIORITY_LOW; priority <= CRUX_THREAD_PRIORITY_HIGH;
         ++priority) {
        worker_observation_t observation = {0};
        crux_thread_options_t options = {.stack_size_bytes = 4096,
                                         .priority = (crux_thread_priority_t)priority};
        crux_thread_t worker;
        zassert_equal(crux_thread_create(&options, observe_native_worker, &observation, &worker),
                      CRUX_OK);
        zassert_equal(crux_thread_join(&worker, 1000), CRUX_OK);
        zassert_equal(observation.result, 0);
        zassert_equal(observation.priority, 6 - priority);
        zassert_true(observation.usable >= 4096);
        zassert_true(observation.unused > 0);
        printk("EXECUTION_PROFILE class=%d native_priority=%d usable=%zu unused=%zu\n", priority,
               observation.priority, observation.usable, observation.unused);
    }
}
ZTEST_SUITE(portable_execution, NULL, NULL, NULL, NULL, NULL);
