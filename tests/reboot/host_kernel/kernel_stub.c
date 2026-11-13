/* See zephyr/kernel.h in this directory. */
#include <zephyr/kernel.h>

#include <errno.h>
#include <time.h>

static struct timespec deadline_after(int64_t ms)
{
    struct timespec at;

    clock_gettime(CLOCK_REALTIME, &at);
    at.tv_sec += (time_t)(ms / 1000);
    at.tv_nsec += (long)(ms % 1000) * 1000000L;
    if (at.tv_nsec >= 1000000000L) {
        at.tv_nsec -= 1000000000L;
        ++at.tv_sec;
    }
    return at;
}

int z_impl_k_mutex_lock(struct k_mutex *mutex, k_timeout_t timeout)
{
    struct timespec at;

    if (timeout.ticks == 0) {
        return pthread_mutex_trylock(&mutex->lock) == 0 ? 0 : -EBUSY;
    }
    if (timeout.ticks < 0) {
        return pthread_mutex_lock(&mutex->lock) == 0 ? 0 : -EINVAL;
    }
    at = deadline_after(timeout.ticks);
    return pthread_mutex_timedlock(&mutex->lock, &at) == 0 ? 0 : -EAGAIN;
}

int k_mutex_unlock(struct k_mutex *mutex)
{
    return pthread_mutex_unlock(&mutex->lock) == 0 ? 0 : -EINVAL;
}

int k_sem_take(struct k_sem *sem, k_timeout_t timeout)
{
    struct timespec at = deadline_after(timeout.ticks > 0 ? timeout.ticks : 0);
    int result = 0;

    pthread_mutex_lock(&sem->lock);
    while (sem->count == 0u && result == 0) {
        if (timeout.ticks == 0) {
            result = -EBUSY;
        } else if (timeout.ticks < 0) {
            pthread_cond_wait(&sem->changed, &sem->lock);
        } else if (pthread_cond_timedwait(&sem->changed, &sem->lock, &at) != 0) {
            result = -EAGAIN;
        }
    }
    if (result == 0) {
        --sem->count;
    }
    pthread_mutex_unlock(&sem->lock);
    return result;
}

void k_sem_give(struct k_sem *sem)
{
    pthread_mutex_lock(&sem->lock);
    if (sem->count < sem->limit) {
        ++sem->count;
    }
    pthread_cond_signal(&sem->changed);
    pthread_mutex_unlock(&sem->lock);
}

struct start {
    k_thread_entry_t entry;
    void *p1;
    void *p2;
    void *p3;
};

static void *trampoline(void *argument)
{
    const struct start *start = argument;

    start->entry(start->p1, start->p2, start->p3);
    return NULL;
}

// NOLINTBEGIN(bugprone-easily-swappable-parameters): the signature of Zephyr's k_thread_create
void *k_thread_create(struct k_thread *thread, k_thread_stack_t *stack, size_t size,
                      k_thread_entry_t entry, void *p1, void *p2, void *p3, int priority,
                      uint32_t options, k_timeout_t delay)
{
    static struct start start; /* The service creates one thread, once. */

    (void)stack;
    (void)size;
    (void)priority;
    (void)options;
    (void)delay;
    start = (struct start){entry, p1, p2, p3};
    pthread_create(&thread->id, NULL, trampoline, &start);
    pthread_detach(thread->id);
    return thread;
}

// NOLINTEND(bugprone-easily-swappable-parameters)

int32_t k_msleep(int32_t ms)
{
    struct timespec span = {ms / 1000, (long)(ms % 1000) * 1000000L};

    nanosleep(&span, NULL);
    return 0;
}
