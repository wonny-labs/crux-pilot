#include "execution_internal.h"

#include <errno.h>
#include <limits.h>

static pthread_mutex_t metadata = PTHREAD_MUTEX_INITIALIZER;

_Static_assert(sizeof(time_t) == sizeof(int64_t), "Host provider requires 64-bit time_t");

bool exec_native_context_allowed(void) {
    return true;
}

void exec_native_metadata_lock(void) {
    (void)pthread_mutex_lock(&metadata);
}

crux_status_t exec_native_metadata_lock_until(const exec_native_deadline_t *deadline) {
    int result = deadline->poll
                     ? pthread_mutex_trylock(&metadata)
                     : pthread_mutex_clocklock(&metadata, CLOCK_MONOTONIC, &deadline->absolute);
    if (result == 0) {
        return CRUX_OK;
    }
    if (result == ETIMEDOUT) {
        return CRUX_ERR_TIMEOUT;
    }
    return result == EBUSY ? CRUX_ERR_BUSY : CRUX_ERR_IO;
}

void exec_native_metadata_unlock(void) {
    (void)pthread_mutex_unlock(&metadata);
}

crux_status_t exec_native_deadline(uint32_t timeout_ms, exec_native_deadline_t *out) {
    if (timeout_ms > INT32_MAX) {
        return CRUX_ERR_OUT_OF_RANGE;
    }
    out->poll = timeout_ms == 0;
    if (clock_gettime(CLOCK_MONOTONIC, &out->absolute) != 0) {
        return CRUX_ERR_IO;
    }
    long nanoseconds = out->absolute.tv_nsec + (long)(timeout_ms % 1000) * 1000000L;
    int64_t seconds = timeout_ms / 1000 + nanoseconds / 1000000000L;
    if (out->absolute.tv_sec < 0 || out->absolute.tv_sec > INT64_MAX - seconds) {
        return CRUX_ERR_OUT_OF_RANGE;
    }
    out->absolute.tv_sec += seconds;
    out->absolute.tv_nsec = nanoseconds % 1000000000L;
    return CRUX_OK;
}

static void *run_thread(void *argument) {
    exec_native_thread_t *thread = argument;
    thread->entry(thread->argument);
    return NULL;
}

crux_status_t exec_native_thread_create(exec_native_thread_t *thread, crux_thread_entry_t entry,
                                        void *argument, uint32_t slot,
                                        const crux_thread_options_t *options) {
    (void)slot;
    /* Functional Host provider uses unprivileged normal scheduling. */
    if (options->stack_size_bytes > 65536) {
        return CRUX_ERR_OUT_OF_RANGE;
    }
    pthread_attr_t attributes;
    if (pthread_attr_init(&attributes) != 0) {
        return CRUX_ERR_NO_RESOURCES;
    }
    size_t actual_stack = 65536;
    int result = pthread_attr_setstacksize(&attributes, actual_stack);
    if (result == 0) {
        thread->entry = entry;
        thread->argument = argument;
        result = pthread_create(&thread->id, &attributes, run_thread, thread);
    }
    (void)pthread_attr_destroy(&attributes);
    if (result == 0) {
        return CRUX_OK;
    }
    return result == EAGAIN || result == ENOMEM ? CRUX_ERR_NO_RESOURCES : CRUX_ERR_IO;
}

bool exec_native_thread_is_current(const exec_native_thread_t *thread) {
    return pthread_equal(thread->id, pthread_self()) != 0;
}

crux_status_t exec_native_thread_join(exec_native_thread_t *thread,
                                      const exec_native_deadline_t *deadline) {
    int result;
    do {
        result = deadline->poll
                     ? pthread_tryjoin_np(thread->id, NULL)
                     : pthread_clockjoin_np(thread->id, NULL, CLOCK_MONOTONIC, &deadline->absolute);
    } while (result == EINTR);
    if (result == 0) {
        return CRUX_OK;
    }
    if (result == EBUSY) {
        return CRUX_ERR_BUSY;
    }
    if (result == ETIMEDOUT) {
        return CRUX_ERR_TIMEOUT;
    }
    return CRUX_ERR_IO;
}
