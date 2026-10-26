/* Private white-box fixture: no production controls or exported test hooks. */
#include "../../components/queue/src/queue.c"
#include <assert.h>
#include <stdio.h>
static bool context = true;
static crux_status_t prepare_result = CRUX_OK, lock_result = CRUX_OK, deadline_result = CRUX_OK;
static unsigned deadline_calls, waits;
static const queue_deadline_t *original;
bool queue_port_context(void)
{
    return context;
}
crux_status_t queue_port_deadline(uint32_t ms, queue_deadline_t *out)
{
    (void)ms;
    ++deadline_calls;
    original = out;
    return deadline_result;
}
crux_status_t queue_port_lock(const queue_deadline_t *deadline)
{
    assert(deadline == NULL || deadline == original);
    return lock_result;
}
void queue_port_unlock(void)
{
}
crux_status_t queue_port_prepare(size_t index)
{
    (void)index;
    return prepare_result;
}
crux_status_t queue_port_wait(size_t index, const queue_deadline_t *deadline)
{
    (void)index;
    assert(deadline == original);
    return ++waits < 3 ? CRUX_OK : CRUX_ERR_TIMEOUT;
}
void queue_port_notify(size_t index)
{
    (void)index;
}
int main(void)
{
    crux_queue_t q = {0};
    prepare_result = CRUX_ERR_IO;
    assert(crux_queue_create(1, sizeof(int), &q) == CRUX_ERR_IO && q.slot == 0);
    prepare_result = CRUX_OK;
    assert(crux_queue_create(1, sizeof(int), &q) == CRUX_OK);
    int value = 123;
    deadline_calls = waits = 0;
    assert(crux_queue_receive(q, &value, sizeof(value), 10) == CRUX_ERR_TIMEOUT);
    assert(value == 123 && deadline_calls == 1 && waits == 3);
    deadline_result = CRUX_ERR_IO;
    assert(crux_queue_receive(q, &value, sizeof(value), 10) == CRUX_ERR_IO && value == 123);
    deadline_result = CRUX_OK;
    lock_result = CRUX_ERR_IO;
    crux_queue_t old = q;
    assert(crux_queue_destroy(&q) == CRUX_ERR_IO && q.generation == old.generation);
    lock_result = CRUX_OK;
    context = false;
    assert(crux_queue_send(q, &value, sizeof(value), 0) == CRUX_ERR_NOT_SUPPORTED);
    assert(crux_queue_receive(q, &value, sizeof(value), 0) == CRUX_ERR_NOT_SUPPORTED);
    assert(crux_queue_destroy(&q) == CRUX_ERR_NOT_SUPPORTED && q.slot == old.slot);
    context = true;
    assert(crux_queue_destroy(&q) == CRUX_OK);
    /* Force representational exhaustion in this isolated private test only. */
    slots[0].generation = UINT32_MAX;
    assert(crux_queue_create(1, sizeof(int), &q) == CRUX_OK && q.slot == 1);
    old = q;
    assert(crux_queue_destroy(&q) == CRUX_OK && slots[0].retired);
    crux_queue_t remaining[3];
    for (unsigned i = 0; i < 3; ++i)
        assert(crux_queue_create(1, sizeof(int), &remaining[i]) == CRUX_OK &&
               remaining[i].slot != 1);
    assert(crux_queue_create(1, sizeof(int), &q) == CRUX_ERR_NO_RESOURCES);
    assert(crux_queue_send(old, &value, sizeof(value), 0) == CRUX_ERR_INVALID_ARGUMENT);
    for (unsigned i = 0; i < 3; ++i)
        assert(crux_queue_destroy(&remaining[i]) == CRUX_OK);
    puts("queue failure injection/deadline/retirement passed");
    return 0;
}
