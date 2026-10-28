#include "queue_port.h"
#include <assert.h>
#include <crux/queue.h>
#include <pthread.h>
#include <semaphore.h>
#include <stdio.h>
static sem_t admitted;
static crux_queue_t queue;
static crux_status_t observed;
static int value;
static uint32_t wait_ms;
crux_status_t __real_crux_queue_wait(size_t index, const queue_deadline_t *deadline);
crux_status_t __wrap_crux_queue_wait(size_t index, const queue_deadline_t *deadline)
{
    assert(sem_post(&admitted) == 0);
    return __real_crux_queue_wait(index, deadline);
}
static void *reader(void *unused)
{
    (void)unused;
    observed = crux_queue_receive(queue, &value, sizeof(value), wait_ms);
    return NULL;
}
static void *writer(void *unused)
{
    (void)unused;
    observed = crux_queue_send(queue, &value, sizeof(value), wait_ms);
    return NULL;
}
int main(void)
{
    assert(sem_init(&admitted, 0, 0) == 0);
    assert(crux_queue_create(1, sizeof(int), &queue) == CRUX_OK);
    pthread_t worker;
    wait_ms = 3000;
    assert(pthread_create(&worker, NULL, reader, NULL) == 0);
    assert(sem_wait(&admitted) == 0);
    crux_queue_t alias = queue;
    assert(crux_queue_destroy(&queue) == CRUX_ERR_BUSY);
    assert(queue.slot == alias.slot && queue.generation == alias.generation);
    int sent = 71;
    assert(crux_queue_send(queue, &sent, sizeof(sent), 3000) == CRUX_OK);
    assert(pthread_join(worker, NULL) == 0);
    assert(observed == CRUX_OK && value == sent);
    assert(crux_queue_send(queue, &sent, sizeof(sent), 0) == CRUX_OK);
    value = 82;
    assert(pthread_create(&worker, NULL, writer, NULL) == 0);
    assert(sem_wait(&admitted) == 0);
    assert(crux_queue_destroy(&queue) == CRUX_ERR_BUSY);
    int got = 0;
    assert(crux_queue_receive(queue, &got, sizeof(got), 3000) == CRUX_OK && got == 71);
    assert(pthread_join(worker, NULL) == 0 && observed == CRUX_OK);
    assert(crux_queue_receive(queue, &got, sizeof(got), 0) == CRUX_OK && got == 82);
    /* Keep metadata locked until poll/finite admission calls return. */
    assert(crux_queue_lock(NULL) == CRUX_OK);
    wait_ms = 0;
    assert(pthread_create(&worker, NULL, reader, NULL) == 0);
    assert(pthread_join(worker, NULL) == 0 && observed == CRUX_ERR_BUSY);
    wait_ms = 10;
    assert(pthread_create(&worker, NULL, reader, NULL) == 0);
    assert(pthread_join(worker, NULL) == 0 && observed == CRUX_ERR_TIMEOUT);
    crux_queue_unlock();
    assert(crux_queue_destroy(&queue) == CRUX_OK);
    assert(crux_queue_create(1, sizeof(int), &queue) == CRUX_OK);
    assert(crux_queue_send(alias, &sent, sizeof(sent), 0) == CRUX_ERR_INVALID_ARGUMENT);
    assert(crux_queue_destroy(&queue) == CRUX_OK);
    assert(sem_destroy(&admitted) == 0);
    puts("queue controlled wait/admission/lifetime passed");
    return 0;
}
