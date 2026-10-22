#include <crux/execution.h>
#include <pthread.h>
#include <semaphore.h>
#include <stdio.h>

/* Hold the real semaphore bookkeeping lock while tokens remain available.
 * A poll must report token availability, not a transient bookkeeping collision. */
static pthread_mutex_t *bookkeeping;
static int capture;
static sem_t ready;
static sem_t release;

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
        (void)sem_post(&release);
    }
    return __real_pthread_mutex_lock(mutex);
}
int __wrap_pthread_mutex_trylock(pthread_mutex_t *mutex) {
    int result = __real_pthread_mutex_trylock(mutex);
    if (mutex == bookkeeping) {
        (void)sem_post(&release);
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
int main(void) {
    if (sem_init(&ready, 0, 0) != 0 || sem_init(&release, 0, 0) != 0) {
        return 1;
    }
    crux_semaphore_t tokens;
    capture = 1;
    if (crux_semaphore_create(2, 3, &tokens) != CRUX_OK || bookkeeping == NULL) {
        return 2;
    }
    pthread_t holder;
    if (pthread_create(&holder, NULL, hold_bookkeeping, NULL) != 0 || sem_wait(&ready) != 0) {
        return 3;
    }
    crux_status_t result = crux_semaphore_take(tokens, 0);
    if (pthread_join(holder, NULL) != 0) {
        return 4;
    }
    if (result != CRUX_OK) {
        fprintf(stderr, "available-token poll returned %d\n", result);
        return 5;
    }
    if (crux_semaphore_take(tokens, 0) != CRUX_OK ||
        crux_semaphore_take(tokens, 0) != CRUX_ERR_BUSY ||
        crux_semaphore_destroy(&tokens) != CRUX_OK) {
        return 6;
    }
    (void)sem_destroy(&ready);
    (void)sem_destroy(&release);
    return 0;
}
