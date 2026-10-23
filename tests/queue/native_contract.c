/* Deterministic pinned-native contract model, complemented by actual ztests. */
#include "queue_port.h"
#include <assert.h>
#include <errno.h>
#include <stdio.h>
extern struct k_mutex crux_queue_metadata;
static int64_t ticks;
bool k_is_in_isr(void) {
    return false;
}
int64_t k_uptime_ticks(void) {
    return ticks;
}
int k_mutex_lock(struct k_mutex *m, k_timeout_t t) {
    (void)t;
    m->owned = true;
    return 0;
}
int k_mutex_unlock(struct k_mutex *m) {
    m->owned = false;
    return 0;
}
int k_condvar_init(struct k_condvar *c) {
    (void)c;
    return 0;
}
int k_condvar_wait(struct k_condvar *c, struct k_mutex *m, k_timeout_t t) {
    (void)c;
    (void)t;
    m->owned = false;
    return -EAGAIN;
}
int k_condvar_broadcast(struct k_condvar *c) {
    (void)c;
    return 0;
}
int main(int argc, char **argv) {
    (void)argv;
    queue_deadline_t deadline;
    if (argc == 1) {
        assert(queue_port_deadline(1, &deadline) == CRUX_OK);
        assert(queue_port_lock(NULL) == CRUX_OK);
        assert(queue_port_prepare(0) == CRUX_OK);
        assert(queue_port_wait(0, &deadline) == CRUX_ERR_TIMEOUT);
        assert(crux_queue_metadata.owned);
        queue_port_unlock();
    } else {
        /* 1 ms rounds to one tick at 128 Hz, plus the current partial tick. */
        ticks = INT64_MAX - 2;
        assert(queue_port_deadline(1, &deadline) == CRUX_ERR_OUT_OF_RANGE);
        ticks = INT64_MAX - 3;
        assert(queue_port_deadline(1, &deadline) == CRUX_OK);
        assert(deadline.timeout.ticks == INT64_MAX - 1);
    }
    puts("queue pinned native timeout contract passed");
    return 0;
}
