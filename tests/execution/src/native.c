#include <errno.h>
#include <stdint.h>
#include <zephyr/kernel.h>
#include <zephyr/ztest.h>

K_THREAD_STACK_DEFINE(worker_stack, 4096);
static struct k_thread worker;
K_MUTEX_DEFINE(mutex);
K_SEM_DEFINE(signal, 0, 1);
K_SEM_DEFINE(entered, 0, 1);
K_MSGQ_DEFINE(messages, sizeof(uint32_t), 1, 4);
static int worker_result;

static void semaphore_worker(void *a, void *b, void *c)
{
    ARG_UNUSED(a);
    ARG_UNUSED(b);
    ARG_UNUSED(c);
    k_msleep(20);
    k_sem_give(&signal);
}

static void mutex_worker(void *a, void *b, void *c)
{
    ARG_UNUSED(a);
    ARG_UNUSED(b);
    ARG_UNUSED(c);
    k_sem_give(&entered);
    worker_result = k_mutex_lock(&mutex, K_MSEC(20));
    if (worker_result == 0) {
        (void)k_mutex_unlock(&mutex);
    }
}

static void periodic_worker(void *a, void *b, void *c)
{
    ARG_UNUSED(a);
    ARG_UNUSED(b);
    ARG_UNUSED(c);
    int64_t start = k_uptime_get();
    int64_t next = start + 10;
    unsigned int skipped = 0;
    for (int i = 0; i < 6; ++i) {
        (void)k_sleep(K_TIMEOUT_ABS_MS(next));
        int64_t released = k_uptime_get();
        zassert_true(released >= next, "release before deadline");
        printk("PROBE native cycle=%d lateness_ms=%lld\n", i, (long long)(released - next));
        if (i == 1) {
            k_msleep(25);
        }
        next += 10;
        int64_t now = k_uptime_get();
        while (next <= now) {
            next += 10;
            ++skipped;
        }
    }
    zassert_true(skipped >= 2, "deliberate overrun must skip elapsed releases");
    printk("PROBE native periodic elapsed_ms=%lld skipped=%u priority=%d stack_bytes=%zu\n",
           (long long)(k_uptime_get() - start), skipped, k_thread_priority_get(k_current_get()),
           sizeof(worker_stack));
}

static k_tid_t start_worker(k_thread_entry_t entry)
{
    return k_thread_create(&worker, worker_stack, K_THREAD_STACK_SIZEOF(worker_stack), entry, NULL,
                           NULL, NULL, 5, 0, K_NO_WAIT);
}

ZTEST(execution_native, test_lifecycle_periodic_and_stack)
{
    k_tid_t tid = start_worker(periodic_worker);
    zassert_equal(k_thread_join(tid, K_SECONDS(2)), 0);
    size_t unused = 0;
    zassert_equal(k_thread_stack_space_get(tid, &unused), 0);
    printk("PROBE native stack_unused_bytes=%zu\n", unused);
    zassert_true(unused > 0);
    tid = start_worker(semaphore_worker);
    zassert_equal(k_thread_join(tid, K_SECONDS(2)), 0);
    zassert_equal(k_sem_take(&signal, K_NO_WAIT), 0);
}

ZTEST(execution_native, test_mutex_contention_timeout_and_recovery)
{
    int original_priority = k_thread_priority_get(k_current_get());
    k_thread_priority_set(k_current_get(), 7);
    zassert_equal(k_mutex_lock(&mutex, K_NO_WAIT), 0);
    k_tid_t tid = start_worker(mutex_worker);
    zassert_equal(k_sem_take(&entered, K_SECONDS(1)), 0);
    zassert_equal(k_thread_priority_get(k_current_get()), 5);
    zassert_equal(k_thread_join(tid, K_SECONDS(2)), 0);
    zassert_equal(worker_result, -EAGAIN);
    zassert_equal(k_mutex_unlock(&mutex), 0);
    tid = start_worker(mutex_worker);
    zassert_equal(k_thread_join(tid, K_SECONDS(2)), 0);
    zassert_equal(worker_result, 0);
    k_sem_reset(&entered);
    k_thread_priority_set(k_current_get(), original_priority);
}

ZTEST(execution_native, test_semaphore_expiry_wakeup_and_capacity)
{
    k_sem_reset(&signal);
    zassert_equal(k_sem_take(&signal, K_NO_WAIT), -EBUSY);
    int64_t start = k_uptime_get();
    zassert_equal(k_sem_take(&signal, K_MSEC(10)), -EAGAIN);
    zassert_true(k_uptime_get() - start >= 10);
    start = k_uptime_get();
    k_tid_t tid = start_worker(semaphore_worker);
    zassert_equal(k_sem_take(&signal, K_SECONDS(1)), 0);
    zassert_true(k_uptime_get() - start >= 20);
    zassert_equal(k_thread_join(tid, K_SECONDS(1)), 0);
    k_sem_give(&signal);
    k_sem_give(&signal);
    zassert_equal(k_sem_count_get(&signal), 1);
    zassert_equal(k_sem_take(&signal, K_NO_WAIT), 0);
}

ZTEST(execution_native, test_bounded_messages)
{
    uint32_t sent = 42;
    uint32_t received = 0;
    zassert_equal(k_msgq_get(&messages, &received, K_NO_WAIT), -ENOMSG);
    zassert_equal(k_msgq_put(&messages, &sent, K_NO_WAIT), 0);
    zassert_equal(k_msgq_put(&messages, &sent, K_NO_WAIT), -ENOMSG);
    zassert_equal(k_msgq_get(&messages, &received, K_NO_WAIT), 0);
    zassert_equal(received, sent);
    zassert_equal(k_msgq_get(&messages, &received, K_MSEC(10)), -EAGAIN);
}

ZTEST_SUITE(execution_native, NULL, NULL, NULL, NULL, NULL);
