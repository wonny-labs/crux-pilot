#include "execution_internal.h"
#include <errno.h>
#include <limits.h>

BUILD_ASSERT(CONFIG_NUM_PREEMPT_PRIORITIES > 6, "Execution requires preemptive priorities 4..6");
K_MUTEX_DEFINE(metadata);
K_THREAD_STACK_ARRAY_DEFINE(worker_stacks, CRUX_EXECUTION_THREAD_COUNT, CRUX_EXECUTION_STACK_BYTES);

bool exec_native_context_allowed(void) {
    return !k_is_in_isr();
}
void exec_native_metadata_lock(void) {
    (void)k_mutex_lock(&metadata, K_FOREVER);
}
void exec_native_metadata_unlock(void) {
    (void)k_mutex_unlock(&metadata);
}

crux_status_t exec_native_deadline(uint32_t timeout_ms, exec_native_deadline_t *out) {
    if (timeout_ms > INT32_MAX)
        return CRUX_ERR_OUT_OF_RANGE;
    if (timeout_ms == 0) {
        out->timeout = K_NO_WAIT;
        return CRUX_OK;
    }
    int64_t now = k_uptime_ticks();
    int64_t duration = k_ms_to_ticks_ceil64(timeout_ms);
    /* One extra tick covers the unobserved fraction of the current tick. */
    if (now > INT64_MAX - duration - 1)
        return CRUX_ERR_OUT_OF_RANGE;
    out->timeout = K_TIMEOUT_ABS_TICKS(now + duration + 1);
    return CRUX_OK;
}
static crux_status_t native_wait_result(int result) {
    if (result == 0)
        return CRUX_OK;
    if (result == -EBUSY)
        return CRUX_ERR_BUSY;
    if (result == -EAGAIN)
        return CRUX_ERR_TIMEOUT;
    return CRUX_ERR_IO;
}
static void run_thread(void *argument, void *unused1, void *unused2) {
    (void)unused1;
    (void)unused2;
    exec_native_thread_t *thread = argument;
    thread->entry(thread->argument);
}
crux_status_t exec_native_thread_create(exec_native_thread_t *thread, crux_thread_entry_t entry,
                                        void *argument, uint32_t slot,
                                        const crux_thread_options_t *options) {
    if (options->stack_size_bytes > CRUX_EXECUTION_STACK_BYTES) {
        return CRUX_ERR_OUT_OF_RANGE;
    }
    thread->entry = entry;
    thread->argument = argument;
    int native_priority = 6 - (int)options->priority;
    k_tid_t id = k_thread_create(&thread->object, worker_stacks[slot],
                                 K_THREAD_STACK_SIZEOF(worker_stacks[slot]), run_thread, thread,
                                 NULL, NULL, native_priority, 0, K_NO_WAIT);
    return id != NULL ? CRUX_OK : CRUX_ERR_NO_RESOURCES;
}
bool exec_native_thread_is_current(const exec_native_thread_t *thread) {
    return k_current_get() == &thread->object;
}
crux_status_t exec_native_thread_join(exec_native_thread_t *thread,
                                      const exec_native_deadline_t *deadline) {
    return native_wait_result(k_thread_join(&thread->object, deadline->timeout));
}
exec_native_identity_t exec_native_current(void) {
    return k_current_get();
}
bool exec_native_identity_equal(exec_native_identity_t first, exec_native_identity_t second) {
    return first == second;
}
crux_status_t exec_native_mutex_create(exec_native_mutex_t *mutex) {
    return k_mutex_init(mutex) == 0 ? CRUX_OK : CRUX_ERR_IO;
}
crux_status_t exec_native_mutex_lock(exec_native_mutex_t *mutex,
                                     const exec_native_deadline_t *deadline) {
    return native_wait_result(k_mutex_lock(mutex, deadline->timeout));
}
crux_status_t exec_native_mutex_unlock(exec_native_mutex_t *mutex) {
    return k_mutex_unlock(mutex) == 0 ? CRUX_OK : CRUX_ERR_IO;
}
crux_status_t exec_native_mutex_destroy(exec_native_mutex_t *mutex) {
    (void)mutex; /* No dynamically allocated kernel state; common code proves quiescence. */
    return CRUX_OK;
}
/* Keep the public initial/maximum order at the native boundary. */
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
crux_status_t exec_native_semaphore_create(exec_native_semaphore_t *semaphore, uint32_t initial,
                                           uint32_t maximum) {
    semaphore->maximum = maximum;
    return k_sem_init(&semaphore->object, initial, maximum) == 0 ? CRUX_OK : CRUX_ERR_IO;
}
crux_status_t exec_native_semaphore_take(exec_native_semaphore_t *semaphore,
                                         const exec_native_deadline_t *deadline) {
    return native_wait_result(k_sem_take(&semaphore->object, deadline->timeout));
}
crux_status_t exec_native_semaphore_give(exec_native_semaphore_t *semaphore) {
    /* Common metadata protection serialises all givers. Takes can only reduce count. */
    if (k_sem_count_get(&semaphore->object) == semaphore->maximum)
        return CRUX_ERR_BUSY;
    k_sem_give(&semaphore->object);
    return CRUX_OK;
}
crux_status_t exec_native_semaphore_destroy(exec_native_semaphore_t *semaphore) {
    (void)semaphore;
    return CRUX_OK;
}
