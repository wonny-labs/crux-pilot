#include "thread_internal.h"
#include <errno.h>
#include <limits.h>

BUILD_ASSERT(CONFIG_NUM_PREEMPT_PRIORITIES > 6, "Thread requires preemptive priorities 4..6");
K_MUTEX_DEFINE(thread_metadata);
K_THREAD_STACK_ARRAY_DEFINE(worker_stacks, CRUX_THREAD_COUNT, CRUX_THREAD_STACK_BYTES);

crux_status_t crux_thread_impl_sleep_until_ns(int64_t deadline_ns)
{
    int64_t ticks;
    crux_status_t status =
        thread_sleep_ns_to_ticks_ceil(deadline_ns, CONFIG_SYS_CLOCK_TICKS_PER_SEC, &ticks);
    if (status != CRUX_OK) {
        return status;
    }
    /* Conversion rejects the native macro's reserved/clamped INT64_MAX value.
     * Common code handles early wake against the same deadline. */
    (void)k_sleep(K_TIMEOUT_ABS_TICKS(ticks));
    return CRUX_OK;
}

void thread_native_metadata_lock(void)
{
    (void)k_mutex_lock(&thread_metadata, K_FOREVER);
}

crux_status_t thread_native_metadata_lock_until(const thread_native_deadline_t *deadline)
{
    int result = k_mutex_lock(&thread_metadata, deadline->timeout);
    if (result == 0)
        return CRUX_OK;
    if (result == -EAGAIN)
        return CRUX_ERR_TIMEOUT;
    if (result == -EBUSY)
        return CRUX_ERR_BUSY;
    return CRUX_ERR_IO;
}

void thread_native_metadata_unlock(void)
{
    (void)k_mutex_unlock(&thread_metadata);
}

crux_status_t thread_native_deadline(uint32_t timeout_ms, thread_native_deadline_t *out)
{
    if (timeout_ms > INT32_MAX)
        return CRUX_ERR_OUT_OF_RANGE;
    out->poll = timeout_ms == 0;
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

static crux_status_t native_wait_result(int result)
{
    if (result == 0)
        return CRUX_OK;
    if (result == -EBUSY)
        return CRUX_ERR_BUSY;
    if (result == -EAGAIN)
        return CRUX_ERR_TIMEOUT;
    return CRUX_ERR_IO;
}

static void run_thread(void *argument, void *unused1, void *unused2)
{
    (void)unused1;
    (void)unused2;
    thread_native_thread_t *thread = argument;
    thread->entry(thread->argument);
}

crux_status_t thread_native_thread_create(thread_native_thread_t *thread, crux_thread_entry_t entry,
                                          void *argument, uint32_t slot,
                                          const crux_thread_options_t *options)
{
    if (options->stack_size_bytes > CRUX_THREAD_STACK_BYTES) {
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

bool thread_native_thread_is_current(const thread_native_thread_t *thread)
{
    return k_current_get() == &thread->object;
}

crux_status_t thread_native_thread_join(thread_native_thread_t *thread,
                                        const thread_native_deadline_t *deadline)
{
    return native_wait_result(k_thread_join(&thread->object, deadline->timeout));
}
