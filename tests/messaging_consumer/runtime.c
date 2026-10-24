#include <crux/queue.h>
#include <crux/sync.h>
#include <crux/thread.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* A synthetic runtime port, not a production runtime compatibility claim. */
enum { PRODUCERS = 2, RECORDS = 32, WAIT_MS = 20 };
typedef struct {
    uint32_t producer;
    uint32_t sequence;
    uint32_t inverse;
    uint32_t payload[5];
} command_t;
typedef struct session session_t;
typedef struct {
    session_t *session;
    uint32_t id;
    int result;
} producer_t;
struct session {
    crux_queue_t queue;
    crux_mutex_t mutex;
    crux_semaphore_t progress;
    bool stop;
    bool continuous;
    producer_t producers[PRODUCERS];
    crux_thread_t threads[PRODUCERS + 1];
    uint32_t received[PRODUCERS];
    int worker_result;
};

static bool stopped(session_t *session, int *result) {
    if (crux_mutex_lock(session->mutex, 500) != CRUX_OK) {
        *result = 1;
        return true;
    }
    bool stop = session->stop;
    if (crux_mutex_unlock(session->mutex) != CRUX_OK) {
        *result = 2;
        return true;
    }
    return stop;
}

static int request_stop(session_t *session) {
    if (crux_mutex_lock(session->mutex, 500) != CRUX_OK)
        return 1;
    session->stop = true;
    return crux_mutex_unlock(session->mutex) == CRUX_OK ? 0 : 2;
}

static void producer(void *argument) {
    producer_t *producer = argument;
    session_t *session = producer->session;
    uint32_t sequence = 0;
    while (session->continuous || sequence < RECORDS) {
        /* Also runs after successful sends: traffic cannot bypass cancellation. */
        if (stopped(session, &producer->result))
            return;
        command_t command = {.producer = producer->id, .sequence = sequence, .inverse = ~sequence};
        for (size_t i = 0; i < 5; ++i)
            command.payload[i] = producer->id * 1000 + sequence * 5 + (uint32_t)i;
        crux_status_t status = crux_queue_send(session->queue, &command, sizeof(command), WAIT_MS);
        /* The caller may reuse its storage as soon as send returns. */
        memset(&command, 0xa5, sizeof(command));
        if (status == CRUX_OK)
            ++sequence;
        else if (status != CRUX_ERR_TIMEOUT && status != CRUX_ERR_BUSY) {
            producer->result = 3;
            return;
        }
    }
}

static void worker(void *argument) {
    session_t *session = argument;
    uint32_t total = 0;
    while (session->continuous || total < PRODUCERS * RECORDS) {
        if (stopped(session, &session->worker_result))
            return;
        command_t command;
        crux_status_t status =
            crux_queue_receive(session->queue, &command, sizeof(command), WAIT_MS);
        if (status == CRUX_ERR_TIMEOUT || status == CRUX_ERR_BUSY)
            continue;
        if (status != CRUX_OK || command.producer >= PRODUCERS ||
            command.sequence != session->received[command.producer] ||
            command.inverse != ~command.sequence) {
            session->worker_result = 4;
            return;
        }
        for (size_t i = 0; i < 5; ++i) {
            if (command.payload[i] !=
                command.producer * 1000 + command.sequence * 5 + (uint32_t)i) {
                session->worker_result = 5;
                return;
            }
        }
        ++session->received[command.producer];
        ++total;
        /* Notification is consuming, coalesced progress, not broadcast state. */
        if (stopped(session, &session->worker_result))
            return;
        status = crux_semaphore_give(session->progress);
        if (status != CRUX_OK && status != CRUX_ERR_BUSY) {
            session->worker_result = 6;
            return;
        }
    }
}

static int run_session(bool continuous) {
    /* Static storage deliberately survives any failed join, including arguments. */
    static session_t session;
    memset(&session, 0, sizeof(session));
    session.continuous = continuous;
    if (crux_queue_create(4, sizeof(command_t), &session.queue) != CRUX_OK)
        return 10;
    if (crux_mutex_create(&session.mutex) != CRUX_OK) {
        (void)crux_queue_destroy(&session.queue);
        return 11;
    }
    if (crux_semaphore_create(0, 128, &session.progress) != CRUX_OK) {
        (void)crux_mutex_destroy(&session.mutex);
        (void)crux_queue_destroy(&session.queue);
        return 12;
    }
    crux_thread_options_t options = {.stack_size_bytes = 4096,
                                     .priority = CRUX_THREAD_PRIORITY_NORMAL};
    size_t created = 0;
    int result = 0;
    for (uint32_t i = 0; i < PRODUCERS; ++i) {
        session.producers[i].session = &session;
        session.producers[i].id = i;
        if (crux_thread_create(&options, producer, &session.producers[i],
                               &session.threads[created]) != CRUX_OK) {
            result = 13;
            break;
        }
        ++created;
    }
    if (result == 0) {
        if (crux_thread_create(&options, worker, &session, &session.threads[created]) != CRUX_OK)
            result = 14;
        else
            ++created;
    }
    uint32_t notifications = continuous ? 16 : PRODUCERS * RECORDS;
    for (uint32_t i = 0; result == 0 && i < notifications; ++i) {
        if (crux_semaphore_take(session.progress, 2000) != CRUX_OK)
            result = 15;
    }
    if (request_stop(&session) != 0)
        result = 16;
    for (size_t i = 0; i < created; ++i) {
        if (crux_thread_join(&session.threads[i], 2000) != CRUX_OK) {
            /* Retain queue, mutex, semaphore, handles and static arguments. */
            return 17;
        }
    }
    if (session.worker_result != 0 || session.producers[0].result != 0 ||
        session.producers[1].result != 0)
        result = 18;
    if (!continuous && (session.received[0] != RECORDS || session.received[1] != RECORDS))
        result = 19;
    if (crux_semaphore_destroy(&session.progress) != CRUX_OK ||
        crux_mutex_destroy(&session.mutex) != CRUX_OK ||
        crux_queue_destroy(&session.queue) != CRUX_OK)
        result = 20;
    return result;
}

static int bounded_backpressure(void) {
    crux_queue_t queue;
    if (crux_queue_create(1, sizeof(uint32_t), &queue) != CRUX_OK)
        return 21;
    uint32_t source = 0x12345678, received = 0xabcdef01;
    int result = 0;
    if (crux_queue_receive(queue, &received, sizeof(received), 0) != CRUX_ERR_BUSY ||
        crux_queue_receive(queue, &received, sizeof(received), 2) != CRUX_ERR_TIMEOUT ||
        received != 0xabcdef01 || crux_queue_send(queue, &source, sizeof(source), 0) != CRUX_OK)
        result = 22;
    source = 0;
    if (result == 0 && (crux_queue_send(queue, &source, sizeof(source), 0) != CRUX_ERR_BUSY ||
                        crux_queue_send(queue, &source, sizeof(source), 2) != CRUX_ERR_TIMEOUT ||
                        crux_queue_receive(queue, &received, sizeof(received), 0) != CRUX_OK ||
                        received != 0x12345678))
        result = 23;
    if (crux_queue_destroy(&queue) != CRUX_OK)
        result = 24;
    return result;
}

typedef struct {
    crux_semaphore_t gate;
    crux_semaphore_t ready;
    crux_thread_t thread;
    int result;
} gated_t;
static void gated_worker(void *argument) {
    gated_t *gated = argument;
    if (crux_semaphore_give(gated->ready) != CRUX_OK ||
        crux_semaphore_take(gated->gate, 2000) != CRUX_OK)
        gated->result = 1;
}
static int join_retains_lifetime(void) {
    static gated_t gated;
    crux_thread_options_t options = {.stack_size_bytes = 4096,
                                     .priority = CRUX_THREAD_PRIORITY_NORMAL};
    if (crux_semaphore_create(0, 1, &gated.gate) != CRUX_OK)
        return 25;
    if (crux_semaphore_create(0, 1, &gated.ready) != CRUX_OK) {
        (void)crux_semaphore_destroy(&gated.gate);
        return 26;
    }
    if (crux_thread_create(&options, gated_worker, &gated, &gated.thread) != CRUX_OK) {
        (void)crux_semaphore_destroy(&gated.ready);
        (void)crux_semaphore_destroy(&gated.gate);
        return 27;
    }
    int result = 0;
    if (crux_semaphore_take(gated.ready, 1000) != CRUX_OK ||
        crux_thread_join(&gated.thread, 1) != CRUX_ERR_TIMEOUT)
        result = 28;
    if (crux_semaphore_give(gated.gate) != CRUX_OK)
        result = 29;
    if (crux_thread_join(&gated.thread, 2000) != CRUX_OK)
        return 30; /* Do not reclaim anything while the worker may remain live. */
    if (gated.result != 0 || crux_semaphore_destroy(&gated.ready) != CRUX_OK ||
        crux_semaphore_destroy(&gated.gate) != CRUX_OK)
        result = 31;
    return result;
}

typedef struct {
    session_t session;
    bool full;
    crux_thread_t thread;
    int result;
} cancellation_t;

static void cancellation_worker(void *argument) {
    cancellation_t *test = argument;
    bool notified = false;
    while (!stopped(&test->session, &test->result)) {
        uint32_t record = 7;
        crux_status_t status =
            test->full ? crux_queue_send(test->session.queue, &record, sizeof(record), WAIT_MS)
                       : crux_queue_receive(test->session.queue, &record, sizeof(record), WAIT_MS);
        if (status != CRUX_ERR_TIMEOUT) {
            test->result = 1;
            return;
        }
        if (stopped(&test->session, &test->result))
            return;
        if (!notified) {
            if (crux_semaphore_give(test->session.progress) != CRUX_OK) {
                test->result = 2;
                return;
            }
            notified = true;
        }
    }
}

static int cancel_unavailable(bool full) {
    static cancellation_t test;
    memset(&test, 0, sizeof(test));
    test.full = full;
    if (crux_queue_create(1, sizeof(uint32_t), &test.session.queue) != CRUX_OK)
        return 32;
    if (crux_mutex_create(&test.session.mutex) != CRUX_OK) {
        (void)crux_queue_destroy(&test.session.queue);
        return 33;
    }
    if (crux_semaphore_create(0, 1, &test.session.progress) != CRUX_OK) {
        (void)crux_mutex_destroy(&test.session.mutex);
        (void)crux_queue_destroy(&test.session.queue);
        return 34;
    }
    uint32_t record = 9;
    int result = 0;
    if (full && crux_queue_send(test.session.queue, &record, sizeof(record), 0) != CRUX_OK)
        result = 35;
    crux_thread_options_t options = {.stack_size_bytes = 4096,
                                     .priority = CRUX_THREAD_PRIORITY_NORMAL};
    bool created = false;
    if (result == 0) {
        if (crux_thread_create(&options, cancellation_worker, &test, &test.thread) != CRUX_OK)
            result = 36;
        else
            created = true;
    }
    /* The notification proves at least one finite unavailable operation expired;
     * it does not claim the next operation is admitted at this instant. */
    if (created && crux_semaphore_take(test.session.progress, 1000) != CRUX_OK)
        result = 37;
    if (request_stop(&test.session) != 0)
        result = 38;
    if (created && crux_thread_join(&test.thread, 2000) != CRUX_OK)
        return 39; /* All referenced storage remains static and unreclaimed. */
    if (test.result != 0 || crux_semaphore_destroy(&test.session.progress) != CRUX_OK ||
        crux_mutex_destroy(&test.session.mutex) != CRUX_OK ||
        crux_queue_destroy(&test.session.queue) != CRUX_OK)
        result = 40;
    return result;
}

int messaging_runtime_run(void) {
    int result = bounded_backpressure();
    if (result == 0)
        result = cancel_unavailable(false);
    if (result == 0)
        result = cancel_unavailable(true);
    if (result == 0)
        result = join_retains_lifetime();
    for (unsigned session = 0; result == 0 && session < 4; ++session)
        result = run_session((session % 2) != 0);
    if (result != 0)
        printf("MESSAGING_CONSUMER failure %d\n", result);
    return result;
}
