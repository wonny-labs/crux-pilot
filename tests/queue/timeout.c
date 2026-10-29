/* Real Host wait, with a controlled scheduling gap before locked cleanup. */
#include "queue_port.h"
#include <assert.h>
#include <crux/queue.h>
#include <errno.h>
#include <pthread.h>
#include <semaphore.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
static sem_t expired, resume_cleanup, reacquiring, completed;
static crux_queue_t queue;
static crux_status_t observed;
static bool sending;
static int record;
/* GNU linker wrapping requires these symbol spellings; keep ordinary C names. */
int real_cond_wait(pthread_cond_t *cond, pthread_mutex_t *mutex,
                   const struct timespec *deadline) __asm__("__real_pthread_cond_timedwait");
int wrapped_cond_wait(pthread_cond_t *cond, pthread_mutex_t *mutex,
                      const struct timespec *deadline) __asm__("__wrap_pthread_cond_timedwait");
int wrapped_cond_wait(pthread_cond_t *cond, pthread_mutex_t *mutex, const struct timespec *deadline)
{
    int result = real_cond_wait(cond, mutex, deadline);
    if (result == ETIMEDOUT) {
        /* libc returns owning the mutex. Release it only in this fixture to
         * expose availability after known expiry, then delay reacquisition. */
        assert(pthread_mutex_unlock(mutex) == 0);
        assert(sem_post(&expired) == 0);
        assert(sem_wait(&resume_cleanup) == 0);
        assert(sem_post(&reacquiring) == 0);
        assert(pthread_mutex_lock(mutex) == 0);
    }
    return result;
}
static void *transfer_worker(void *unused)
{
    (void)unused;
    observed = sending ? crux_queue_send(queue, &record, sizeof(record), 2)
                       : crux_queue_receive(queue, &record, sizeof(record), 2);
    assert(sem_post(&completed) == 0);
    return NULL;
}
int main(int argc, char **argv)
{
    sending = argc == 2 && strcmp(argv[1], "send") == 0;
    assert(sem_init(&expired, 0, 0) == 0);
    assert(sem_init(&resume_cleanup, 0, 0) == 0);
    assert(sem_init(&reacquiring, 0, 0) == 0);
    assert(sem_init(&completed, 0, 0) == 0);
    assert(crux_queue_create(2, sizeof(int), &queue) == CRUX_OK);
    int first = 11, second = 22;
    if (sending) {
        assert(crux_queue_send(queue, &first, sizeof(first), 0) == CRUX_OK);
        assert(crux_queue_send(queue, &second, sizeof(second), 0) == CRUX_OK);
    }
    record = sending ? 33 : -123;
    pthread_t worker;
    assert(pthread_create(&worker, NULL, transfer_worker, NULL) == 0);
    assert(sem_wait(&expired) == 0);
    crux_queue_t alias = queue;
    assert(crux_queue_destroy(&queue) == CRUX_ERR_BUSY);
    assert(queue.slot == alias.slot && queue.generation == alias.generation);
    int got = 0;
    if (sending) {
        assert(crux_queue_receive(queue, &got, sizeof(got), 0) == CRUX_OK && got == 11);
    } else {
        first = 55;
        second = 66;
        assert(crux_queue_send(queue, &first, sizeof(first), 0) == CRUX_OK);
        assert(crux_queue_send(queue, &second, sizeof(second), 0) == CRUX_OK);
    }
    assert(crux_queue_lock(NULL) == CRUX_OK);
    assert(sem_post(&resume_cleanup) == 0);
    assert(sem_wait(&reacquiring) == 0);
    assert(sem_trywait(&completed) == -1 && errno == EAGAIN);
    crux_queue_unlock();
    assert(pthread_join(worker, NULL) == 0);
    assert(observed == CRUX_ERR_TIMEOUT);
    if (sending) {
        int later = 44;
        assert(crux_queue_send(queue, &later, sizeof(later), 0) == CRUX_OK);
        assert(crux_queue_receive(queue, &got, sizeof(got), 0) == CRUX_OK && got == 22);
        assert(crux_queue_receive(queue, &got, sizeof(got), 0) == CRUX_OK && got == 44);
    } else {
        assert(record == -123);
        assert(crux_queue_receive(queue, &got, sizeof(got), 0) == CRUX_OK && got == 55);
        assert(crux_queue_receive(queue, &got, sizeof(got), 0) == CRUX_OK && got == 66);
    }
    assert(crux_queue_receive(queue, &got, sizeof(got), 0) == CRUX_ERR_BUSY);
    assert(crux_queue_destroy(&queue) == CRUX_OK && queue.slot == 0);
    assert(crux_queue_create(2, sizeof(int), &queue) == CRUX_OK);
    assert(crux_queue_send(alias, &first, sizeof(first), 0) == CRUX_ERR_INVALID_ARGUMENT);
    assert(crux_queue_send(queue, &first, sizeof(first), 0) == CRUX_OK);
    assert(crux_queue_receive(queue, &got, sizeof(got), 0) == CRUX_OK && got == first);
    assert(crux_queue_destroy(&queue) == CRUX_OK);
    assert(sem_destroy(&expired) == 0);
    assert(sem_destroy(&resume_cleanup) == 0);
    assert(sem_destroy(&reacquiring) == 0);
    assert(sem_destroy(&completed) == 0);
    puts(sending ? "expired sender cannot commit; FIFO/lifetime/reuse passed"
                 : "expired receiver cannot consume; FIFO/lifetime/reuse passed");
    return 0;
}
