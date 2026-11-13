/* See compat/pthread.h. */
#include "compat/pthread.h"

#include <errno.h>

#define SLOTS 8
#define STACK_BYTES 16384
/* Equal to the priority of a main thread's peers; the service's worker is at 6. */
#define THREAD_PRIORITY 5

typedef struct {
    struct k_thread thread;
    struct k_sem done;
    void *(*start)(void *);
    void *argument;
    void *result;
    atomic_t used;
} slot_t;

static slot_t slots[SLOTS];
K_THREAD_STACK_ARRAY_DEFINE(stacks, SLOTS, STACK_BYTES);

static void entry(void *slot_pointer, void *unused2, void *unused3)
{
    slot_t *slot = slot_pointer;

    (void)unused2;
    (void)unused3;
    slot->result = slot->start(slot->argument);
    k_sem_give(&slot->done);
}

int pthread_create(pthread_t *thread, const pthread_attr_t *attributes, void *(*start)(void *),
                   void *argument)
{
    (void)attributes;
    for (unsigned index = 0; index < SLOTS; ++index) {
        slot_t *slot = &slots[index];

        if (!atomic_cas(&slot->used, 0, 1)) {
            continue;
        }
        slot->start = start;
        slot->argument = argument;
        slot->result = NULL;
        k_sem_init(&slot->done, 0, 1);
        *thread = index;
        (void)k_thread_create(&slot->thread, stacks[index], K_THREAD_STACK_SIZEOF(stacks[index]),
                              entry, slot, NULL, NULL, THREAD_PRIORITY, 0, K_NO_WAIT);
        return 0;
    }
    return EAGAIN;
}

int pthread_join(pthread_t thread, void **result)
{
    slot_t *slot = &slots[thread];

    (void)k_sem_take(&slot->done, K_FOREVER);
    (void)k_thread_join(&slot->thread, K_FOREVER);
    if (result != NULL) {
        *result = slot->result;
    }
    atomic_set(&slot->used, 0);
    return 0;
}

static void mutex_ready(pthread_mutex_t *mutex)
{
    if (atomic_get(&mutex->state) == 2) {
        return;
    }
    k_sched_lock();
    if (atomic_get(&mutex->state) == 0) {
        k_mutex_init(&mutex->mutex);
        atomic_set(&mutex->state, 2);
    }
    k_sched_unlock();
}

int pthread_mutex_lock(pthread_mutex_t *mutex)
{
    mutex_ready(mutex);
    return k_mutex_lock(&mutex->mutex, K_FOREVER);
}

int pthread_mutex_unlock(pthread_mutex_t *mutex)
{
    return k_mutex_unlock(&mutex->mutex);
}

int pthread_barrier_init(pthread_barrier_t *barrier, const pthread_barrierattr_t *attributes,
                         unsigned count)
{
    (void)attributes;
    k_mutex_init(&barrier->mutex);
    k_condvar_init(&barrier->changed);
    barrier->count = count;
    barrier->waiting = 0;
    barrier->generation = 0;
    return 0;
}

int pthread_barrier_wait(pthread_barrier_t *barrier)
{
    unsigned generation;

    k_mutex_lock(&barrier->mutex, K_FOREVER);
    generation = barrier->generation;
    if (++barrier->waiting == barrier->count) {
        barrier->waiting = 0;
        ++barrier->generation;
        k_condvar_broadcast(&barrier->changed);
    } else {
        while (generation == barrier->generation) {
            k_condvar_wait(&barrier->changed, &barrier->mutex, K_FOREVER);
        }
    }
    k_mutex_unlock(&barrier->mutex);
    return 0;
}

int pthread_barrier_destroy(pthread_barrier_t *barrier)
{
    (void)barrier;
    return 0;
}

int reboot_native_nanosleep(const struct timespec *request, struct timespec *remaining)
{
    (void)remaining;
    (void)k_sleep(K_NSEC((int64_t)request->tv_sec * 1000000000LL + request->tv_nsec));
    return 0;
}
