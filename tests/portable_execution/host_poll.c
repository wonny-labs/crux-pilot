#include <crux/sync.h>
#include <pthread.h>
#include <semaphore.h>
#include <stdio.h>
#include <time.h>

/* Hold the real semaphore bookkeeping lock while tokens remain available. */
static pthread_mutex_t *bookkeeping;
static int capture;
static sem_t ready;
static sem_t attempted;
static sem_t release;
static sem_t completed;
static crux_semaphore_t tokens;
static crux_status_t poll_result;

/* GNU ld --wrap requires these reserved linker symbol names. */
// NOLINTBEGIN(bugprone-reserved-identifier)
int __real_pthread_mutex_init(pthread_mutex_t *, const pthread_mutexattr_t *);
int __real_pthread_mutex_lock(pthread_mutex_t *);
int __real_pthread_mutex_trylock(pthread_mutex_t *);

int __wrap_pthread_mutex_init(pthread_mutex_t *mutex, const pthread_mutexattr_t *attr) {
    int result = __real_pthread_mutex_init(mutex, attr);
    if (capture && result == 0) {
        bookkeeping = mutex;
        capture = 0;
    }
    return result;
}
int __wrap_pthread_mutex_lock(pthread_mutex_t *mutex) {
    if (mutex == bookkeeping) {
        (void)sem_post(&attempted);
    }
    return __real_pthread_mutex_lock(mutex);
}
int __wrap_pthread_mutex_trylock(pthread_mutex_t *mutex) {
    int result = __real_pthread_mutex_trylock(mutex);
    if (mutex == bookkeeping) {
        (void)sem_post(&attempted);
    }
    return result;
}
// NOLINTEND(bugprone-reserved-identifier)

static void *hold_bookkeeping(void *unused) {
    (void)unused;
    (void)__real_pthread_mutex_lock(bookkeeping);
    (void)sem_post(&ready);
    (void)sem_wait(&release);
    (void)pthread_mutex_unlock(bookkeeping);
    return NULL;
}

static void *poll_available_token(void *unused) {
    (void)unused;
    poll_result = crux_semaphore_take(tokens, 0);
    (void)sem_post(&completed);
    return NULL;
}

static int wait_for(sem_t *semaphore, long milliseconds) {
    struct timespec deadline;
    if (clock_gettime(CLOCK_REALTIME, &deadline) != 0) {
        return -1;
    }
    deadline.tv_nsec += milliseconds * 1000000L;
    deadline.tv_sec += deadline.tv_nsec / 1000000000L;
    deadline.tv_nsec %= 1000000000L;
    return sem_timedwait(semaphore, &deadline);
}

int main(void) {
    if (sem_init(&ready, 0, 0) != 0 || sem_init(&attempted, 0, 0) != 0 ||
        sem_init(&release, 0, 0) != 0 || sem_init(&completed, 0, 0) != 0) {
        return 1;
    }
    capture = 1;
    if (crux_semaphore_create(2, 3, &tokens) != CRUX_OK || bookkeeping == NULL) {
        return 2;
    }
    pthread_t holder;
    if (pthread_create(&holder, NULL, hold_bookkeeping, NULL) != 0 || sem_wait(&ready) != 0) {
        return 3;
    }
    pthread_t poller;
    if (pthread_create(&poller, NULL, poll_available_token, NULL) != 0 ||
        wait_for(&attempted, 1000) != 0) {
        return 4;
    }

    /* The poll must finish while the internal lock is still held. */
    int finished_while_held = wait_for(&completed, 500) == 0;
    (void)sem_post(&release);
    if (pthread_join(holder, NULL) != 0 || pthread_join(poller, NULL) != 0) {
        return 5;
    }
    if (!finished_while_held || poll_result != CRUX_ERR_BUSY) {
        fprintf(stderr, "contended available-token poll returned %d (finished=%d)\n", poll_result,
                finished_while_held);
        return 6;
    }
    if (crux_semaphore_take(tokens, 0) != CRUX_OK || crux_semaphore_take(tokens, 0) != CRUX_OK ||
        crux_semaphore_take(tokens, 0) != CRUX_ERR_BUSY ||
        crux_semaphore_destroy(&tokens) != CRUX_OK) {
        return 7;
    }
    (void)sem_destroy(&ready);
    (void)sem_destroy(&attempted);
    (void)sem_destroy(&release);
    (void)sem_destroy(&completed);
    return 0;
}
