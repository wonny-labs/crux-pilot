#include "queue_port.h"
#include <crux/queue.h>
#include <errno.h>
#include <zephyr/ztest.h>
K_THREAD_STACK_DEFINE(worker_stack, 2048);
static struct k_thread worker;
K_SEM_DEFINE(waiting, 0, 1);
static crux_queue_t q;
static crux_status_t observed;
static int record;
static uint32_t wait_ms = 2000;
static bool trace_wait;
static bool pause_timeout;
K_SEM_DEFINE(timeout_seen, 0, 1);
K_SEM_DEFINE(resume_timeout, 0, 1);
crux_status_t __real_queue_port_wait(size_t index, const queue_deadline_t *deadline);
crux_status_t __wrap_queue_port_wait(size_t index, const queue_deadline_t *deadline) {
    if (trace_wait)
        k_sem_give(&waiting);
    return __real_queue_port_wait(index, deadline);
}
K_SEM_DEFINE(reacquiring, 0, 1);
K_SEM_DEFINE(completed, 0, 1);
int __real_z_impl_k_condvar_wait(struct k_condvar *condvar, struct k_mutex *mutex,
                                 k_timeout_t timeout);
int __wrap_z_impl_k_condvar_wait(struct k_condvar *condvar, struct k_mutex *mutex,
                                 k_timeout_t timeout) {
    int result = __real_z_impl_k_condvar_wait(condvar, mutex, timeout);
    if (pause_timeout && result == -EAGAIN) {
        /* Actual pinned kernel timeout returns without the mutex. Pause before
         * the provider regains it; the admitted operation must remain live. */
        k_sem_give(&timeout_seen);
        (void)k_sem_take(&resume_timeout, K_SECONDS(3));
        k_sem_give(&reacquiring);
    }
    return result;
}
static void receive_worker(void *a, void *b, void *c) {
    ARG_UNUSED(a);
    ARG_UNUSED(b);
    ARG_UNUSED(c);
    observed = crux_queue_receive(q, &record, sizeof(record), wait_ms);
    k_sem_give(&completed);
}
static void send_worker(void *a, void *b, void *c) {
    ARG_UNUSED(a);
    ARG_UNUSED(b);
    ARG_UNUSED(c);
    observed = crux_queue_send(q, &record, sizeof(record), wait_ms);
    k_sem_give(&completed);
}
ZTEST(queue, test_waiter_lifecycle_and_wake) {
    trace_wait = true;
    wait_ms = 2000;
    k_sem_reset(&waiting);
    zassert_equal(crux_queue_create(1, sizeof(int), &q), CRUX_OK);
    k_thread_create(&worker, worker_stack, K_THREAD_STACK_SIZEOF(worker_stack), receive_worker,
                    NULL, NULL, NULL, 5, 0, K_NO_WAIT);
    zassert_equal(k_sem_take(&waiting, K_SECONDS(2)), 0);
    crux_queue_t alias = q;
    zassert_equal(crux_queue_destroy(&q), CRUX_ERR_BUSY);
    zassert_equal(q.slot, alias.slot);
    zassert_equal(q.generation, alias.generation);
    int value = 77;
    zassert_equal(crux_queue_send(q, &value, sizeof(value), 2000), CRUX_OK);
    zassert_equal(k_thread_join(&worker, K_SECONDS(2)), 0);
    zassert_equal(observed, CRUX_OK);
    zassert_equal(record, 77);
    zassert_equal(crux_queue_send(q, &value, sizeof(value), 0), CRUX_OK);
    record = 88;
    k_thread_create(&worker, worker_stack, K_THREAD_STACK_SIZEOF(worker_stack), send_worker, NULL,
                    NULL, NULL, 5, 0, K_NO_WAIT);
    zassert_equal(k_sem_take(&waiting, K_SECONDS(2)), 0);
    zassert_equal(crux_queue_destroy(&q), CRUX_ERR_BUSY);
    zassert_equal(crux_queue_receive(q, &value, sizeof(value), 2000), CRUX_OK);
    zassert_equal(value, 77);
    zassert_equal(k_thread_join(&worker, K_SECONDS(2)), 0);
    zassert_equal(observed, CRUX_OK);
    zassert_equal(crux_queue_receive(q, &value, sizeof(value), 0), CRUX_OK);
    zassert_equal(value, 88);
    zassert_equal(crux_queue_destroy(&q), CRUX_OK);
    trace_wait = false;
}
ZTEST(queue, test_tick_timeout_no_early_expiry) {
    crux_queue_t queue;
    zassert_equal(crux_queue_create(1, sizeof(int), &queue), CRUX_OK);
    int value = 123;
    int64_t before = k_uptime_ticks();
    zassert_equal(crux_queue_receive(queue, &value, sizeof(value), 17), CRUX_ERR_TIMEOUT);
    zassert_true(k_ticks_to_ms_floor64(k_uptime_ticks() - before) >= 17);
    zassert_equal(value, 123);
    zassert_equal(crux_queue_destroy(&queue), CRUX_OK);
}

extern struct k_mutex crux_queue_metadata;
ZTEST(queue, test_native_timeout_retains_metadata_ownership) {
    queue_deadline_t deadline;
    zassert_equal(queue_port_deadline(2, &deadline), CRUX_OK);
    zassert_equal(queue_port_lock(NULL), CRUX_OK);
    zassert_equal(queue_port_prepare(0), CRUX_OK);
    crux_status_t result = queue_port_wait(0, &deadline);
    bool owned = crux_queue_metadata.owner == k_current_get();
    /* Release only if the native port actually retained ownership. */
    if (owned)
        queue_port_unlock();
    zassert_equal(result, CRUX_ERR_TIMEOUT);
    zassert_true(owned, "timeout bookkeeping must retain the metadata mutex");
}

static void expired_cleanup(bool sending) {
    k_sem_reset(&timeout_seen);
    k_sem_reset(&resume_timeout);
    k_sem_reset(&reacquiring);
    k_sem_reset(&completed);
    zassert_equal(crux_queue_create(2, sizeof(int), &q), CRUX_OK);
    int first = 11, second = 22;
    if (sending) {
        zassert_equal(crux_queue_send(q, &first, sizeof(first), 0), CRUX_OK);
        zassert_equal(crux_queue_send(q, &second, sizeof(second), 0), CRUX_OK);
    }
    record = sending ? 33 : -123;
    pause_timeout = true;
    wait_ms = 2;
    k_thread_create(&worker, worker_stack, K_THREAD_STACK_SIZEOF(worker_stack),
                    sending ? send_worker : receive_worker, NULL, NULL, NULL, 5, 0, K_NO_WAIT);
    zassert_equal(k_sem_take(&timeout_seen, K_SECONDS(3)), 0);
    crux_queue_t alias = q;
    zassert_equal(crux_queue_destroy(&q), CRUX_ERR_BUSY);
    zassert_equal(q.slot, alias.slot);
    zassert_equal(q.generation, alias.generation);
    int got = 0;
    if (sending) {
        zassert_equal(crux_queue_receive(q, &got, sizeof(got), 0), CRUX_OK);
        zassert_equal(got, 11);
    } else {
        first = 55;
        second = 66;
        zassert_equal(crux_queue_send(q, &first, sizeof(first), 0), CRUX_OK);
        zassert_equal(crux_queue_send(q, &second, sizeof(second), 0), CRUX_OK);
    }
    zassert_equal(queue_port_lock(NULL), CRUX_OK);
    k_sem_give(&resume_timeout);
    zassert_equal(k_sem_take(&reacquiring, K_SECONDS(3)), 0);
    zassert_equal(k_sem_take(&completed, K_NO_WAIT), -EBUSY);
    queue_port_unlock();
    zassert_equal(k_thread_join(&worker, K_SECONDS(3)), 0);
    pause_timeout = false;
    /* Preserve teardown even on RED: capture effects before assertions. */
    int later = 44, drained[2] = {0};
    crux_status_t later_result = CRUX_OK;
    if (sending)
        later_result = crux_queue_send(q, &later, sizeof(later), 0);
    crux_status_t first_result = crux_queue_receive(q, &drained[0], sizeof(int), 0);
    crux_status_t second_result = crux_queue_receive(q, &drained[1], sizeof(int), 0);
    crux_status_t empty_result = crux_queue_receive(q, &got, sizeof(got), 0);
    zassert_equal(crux_queue_destroy(&q), CRUX_OK);
    zassert_equal(q.slot, 0);
    zassert_equal(crux_queue_create(2, sizeof(int), &q), CRUX_OK);
    zassert_equal(crux_queue_send(alias, &first, sizeof(first), 0), CRUX_ERR_INVALID_ARGUMENT);
    zassert_equal(crux_queue_send(q, &first, sizeof(first), 0), CRUX_OK);
    zassert_equal(crux_queue_receive(q, &got, sizeof(got), 0), CRUX_OK);
    zassert_equal(got, first);
    zassert_equal(crux_queue_destroy(&q), CRUX_OK);
    zassert_equal(observed, CRUX_ERR_TIMEOUT);
    zassert_equal(record, sending ? 33 : -123);
    zassert_equal(later_result, CRUX_OK);
    zassert_equal(first_result, CRUX_OK);
    zassert_equal(second_result, CRUX_OK);
    zassert_equal(drained[0], sending ? 22 : 55);
    zassert_equal(drained[1], sending ? 44 : 66);
    zassert_equal(empty_result, CRUX_ERR_BUSY);
}
ZTEST(queue, test_expired_receive_preserves_late_fifo) {
    expired_cleanup(false);
}
ZTEST(queue, test_expired_send_preserves_late_fifo) {
    expired_cleanup(true);
}

typedef struct {
    unsigned producer, sequence, check;
} native_record_t;
K_THREAD_STACK_ARRAY_DEFINE(mpmc_stacks, 4, 2048);
static struct k_thread mpmc_threads[4];
static atomic_t seen[2][32];
static atomic_t failures;
static crux_queue_t mpmc_queue;
K_SEM_DEFINE(mpmc_start, 0, 4);
static void mpmc_producer(void *id, void *unused1, void *unused2) {
    ARG_UNUSED(unused1);
    ARG_UNUSED(unused2);
    unsigned producer = (unsigned)(uintptr_t)id;
    (void)k_sem_take(&mpmc_start, K_FOREVER);
    for (unsigned seq = 0; seq < 32; ++seq) {
        native_record_t r = {producer, seq, producer * 1000U + seq};
        if (crux_queue_send(mpmc_queue, &r, sizeof(r), 2000) != CRUX_OK) {
            atomic_inc(&failures);
            return;
        }
    }
}
static void mpmc_consumer(void *unused0, void *unused1, void *unused2) {
    ARG_UNUSED(unused0);
    ARG_UNUSED(unused1);
    ARG_UNUSED(unused2);
    (void)k_sem_take(&mpmc_start, K_FOREVER);
    for (unsigned n = 0; n < 32; ++n) {
        native_record_t r;
        if (crux_queue_receive(mpmc_queue, &r, sizeof(r), 2000) != CRUX_OK) {
            atomic_inc(&failures);
            return;
        }
        if (r.producer >= 2 || r.sequence >= 32 || r.check != r.producer * 1000U + r.sequence) {
            atomic_inc(&failures);
            return;
        }
        if (atomic_inc(&seen[r.producer][r.sequence]) != 0)
            atomic_inc(&failures);
    }
}
ZTEST(queue, test_native_mpmc_integrity) {
    zassert_equal(crux_queue_create(8, sizeof(native_record_t), &mpmc_queue), CRUX_OK);
    for (unsigned i = 0; i < 4; ++i) {
        k_thread_create(&mpmc_threads[i], mpmc_stacks[i], K_THREAD_STACK_SIZEOF(mpmc_stacks[i]),
                        i < 2 ? mpmc_producer : mpmc_consumer, (void *)(uintptr_t)i, NULL, NULL, 5,
                        0, K_NO_WAIT);
    }
    for (unsigned i = 0; i < 4; ++i)
        k_sem_give(&mpmc_start);
    for (unsigned i = 0; i < 4; ++i)
        zassert_equal(k_thread_join(&mpmc_threads[i], K_SECONDS(3)), 0);
    zassert_equal(atomic_get(&failures), 0);
    for (unsigned p = 0; p < 2; ++p)
        for (unsigned n = 0; n < 32; ++n)
            zassert_equal(atomic_get(&seen[p][n]), 1);
    zassert_equal(crux_queue_destroy(&mpmc_queue), CRUX_OK);
}
