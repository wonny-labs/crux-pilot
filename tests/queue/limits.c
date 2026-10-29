#include <assert.h>
#include <crux/queue.h>
#include <limits.h>
#include <stdio.h>
int main(void)
{
    crux_queue_t q = {71, 42}, slots[4];
    assert(crux_queue_create(0, 1, &q) == CRUX_ERR_INVALID_ARGUMENT && q.slot == 71 &&
           q.generation == 42);
    assert(crux_queue_create(1, 0, &q) == CRUX_ERR_INVALID_ARGUMENT);
    assert(crux_queue_create(17, 1, &q) == CRUX_ERR_OUT_OF_RANGE);
    assert(crux_queue_create(1, 65, &q) == CRUX_ERR_OUT_OF_RANGE);
    assert(crux_queue_create(SIZE_MAX, SIZE_MAX, &q) == CRUX_ERR_OUT_OF_RANGE);
    assert(crux_queue_create(1, 1, NULL) == CRUX_ERR_INVALID_ARGUMENT);
    for (int i = 0; i < 4; ++i)
        assert(crux_queue_create(16, 64, &slots[i]) == CRUX_OK);
    q = slots[0];
    assert(crux_queue_create(1, 1, &q) == CRUX_ERR_NO_RESOURCES);
    assert(q.slot == slots[0].slot && q.generation == slots[0].generation);
    unsigned char source[64] = {9}, dest[64] = {3};
    assert(crux_queue_send(slots[0], source, 63, 0) == CRUX_ERR_INVALID_ARGUMENT);
    assert(crux_queue_send(slots[0], NULL, 64, 0) == CRUX_ERR_INVALID_ARGUMENT);
    assert(crux_queue_receive(slots[0], dest, 64, UINT32_MAX) == CRUX_ERR_OUT_OF_RANGE);
    assert(crux_queue_receive(slots[0], dest, 64, 2) == CRUX_ERR_TIMEOUT && dest[0] == 3);
    assert(crux_queue_send(slots[0], source, 64, 0) == CRUX_OK);
    assert(crux_queue_receive(slots[0], dest, 63, 0) == CRUX_ERR_INVALID_ARGUMENT && dest[0] == 3);
    assert(crux_queue_receive(slots[0], dest, 64, 0) == CRUX_OK && dest[0] == 9);
    crux_queue_t invalid = {0};
    assert(crux_queue_destroy(&invalid) == CRUX_ERR_INVALID_ARGUMENT);
    crux_queue_t stale = slots[0];
    for (int i = 0; i < 4; ++i)
        assert(crux_queue_destroy(&slots[i]) == CRUX_OK);
    assert(crux_queue_create(1, 1, &q) == CRUX_OK);
    assert(crux_queue_destroy(&stale) == CRUX_ERR_INVALID_ARGUMENT && stale.generation != 0);
    assert(crux_queue_destroy(&q) == CRUX_OK);
    puts("queue limits/failure atomicity/stale handles passed");
    return 0;
}
