#include <crux/queue.h>
#include <zephyr/irq_offload.h>
#include <zephyr/ztest.h>
static crux_status_t irq_status;
static void irq_call(const void *unused) {
    (void)unused;
    crux_queue_t q = {0};
    irq_status = crux_queue_create(1, sizeof(int), &q);
}
ZTEST(queue, test_copy_fifo_and_poll) {
    crux_queue_t q;
    zassert_equal(crux_queue_create(1, sizeof(int), &q), CRUX_OK);
    int a = 7, b = 0;
    zassert_equal(crux_queue_send(q, &a, sizeof(a), 0), CRUX_OK);
    a = 11;
    zassert_equal(crux_queue_send(q, &a, sizeof(a), 0), CRUX_ERR_BUSY);
    zassert_equal(crux_queue_receive(q, &b, sizeof(b), 0), CRUX_OK);
    zassert_equal(b, 7);
    zassert_equal(crux_queue_receive(q, &b, sizeof(b), 2), CRUX_ERR_TIMEOUT);
    zassert_equal(b, 7);
    zassert_equal(crux_queue_destroy(&q), CRUX_OK);
}
ZTEST(queue, test_isr_rejection) {
    irq_offload(irq_call, NULL);
    zassert_equal(irq_status, CRUX_ERR_NOT_SUPPORTED);
}
ZTEST_SUITE(queue, NULL, NULL, NULL, NULL, NULL);
