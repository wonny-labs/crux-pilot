#include <assert.h>
#include <crux/queue.h>
#include <stdint.h>
#include <stdio.h>
int main(void)
{
    crux_queue_t q = {0};
    assert(crux_queue_create(2, sizeof(uint32_t), &q) == CRUX_OK);
    uint32_t a = 42, b = 0;
    assert(crux_queue_send(q, &a, sizeof(a), 0) == CRUX_OK);
    a = 99;
    assert(crux_queue_send(q, &a, sizeof(a), 0) == CRUX_OK);
    assert(crux_queue_send(q, &a, sizeof(a), 0) == CRUX_ERR_BUSY);
    assert(crux_queue_receive(q, &b, sizeof(b), 0) == CRUX_OK && b == 42);
    assert(crux_queue_receive(q, &b, sizeof(b), 0) == CRUX_OK && b == 99);
    assert(crux_queue_receive(q, &b, sizeof(b), 0) == CRUX_ERR_BUSY && b == 99);
    assert(crux_queue_destroy(&q) == CRUX_OK);
    puts("queue copy/FIFO/capacity passed");
    return 0;
}
