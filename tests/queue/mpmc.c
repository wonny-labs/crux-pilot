#include <assert.h>
#include <crux/queue.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
typedef struct {
    unsigned producer, sequence, check;
} record_t;
static crux_queue_t queue;
static atomic_uint seen[2][200];
static void *produce(void *argument) {
    unsigned producer = *(unsigned *)argument;
    for (unsigned seq = 0; seq < 200; ++seq) {
        record_t record = {producer, seq, producer * 1000U + seq};
        assert(crux_queue_send(queue, &record, sizeof(record), 3000) == CRUX_OK);
    }
    return NULL;
}
static void *consume(void *unused) {
    (void)unused;
    for (unsigned n = 0; n < 200; ++n) {
        record_t record;
        assert(crux_queue_receive(queue, &record, sizeof(record), 3000) == CRUX_OK);
        assert(record.producer < 2 && record.sequence < 200);
        assert(record.check == record.producer * 1000U + record.sequence);
        assert(atomic_fetch_add(&seen[record.producer][record.sequence], 1) == 0);
    }
    return NULL;
}
int main(void) {
    assert(crux_queue_create(8, sizeof(record_t), &queue) == CRUX_OK);
    pthread_t threads[4];
    unsigned ids[2] = {0, 1};
    assert(pthread_create(&threads[0], NULL, produce, &ids[0]) == 0);
    assert(pthread_create(&threads[1], NULL, produce, &ids[1]) == 0);
    assert(pthread_create(&threads[2], NULL, consume, NULL) == 0);
    assert(pthread_create(&threads[3], NULL, consume, NULL) == 0);
    for (unsigned i = 0; i < 4; ++i)
        assert(pthread_join(threads[i], NULL) == 0);
    for (unsigned p = 0; p < 2; ++p)
        for (unsigned s = 0; s < 200; ++s)
            assert(atomic_load(&seen[p][s]) == 1);
    assert(crux_queue_destroy(&queue) == CRUX_OK);
    puts("queue MPMC copied integrity passed");
    return 0;
}
