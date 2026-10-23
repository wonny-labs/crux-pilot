/* Original synthetic runtime consumer; no production-runtime source or ABI. */
#include "runtime_fixture.h"

#include <crux/execution.h>

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#define WAIT_MS 3000U
#define EXPIRY_MS 2U
#define TASK_LIMIT 8U

/* Runtime-owned request vocabulary, translated at the portable boundary. */
typedef enum { RUNTIME_BACKGROUND, RUNTIME_REGULAR, RUNTIME_URGENT } runtime_priority_t;
typedef enum {
    RUNTIME_OK,
    RUNTIME_RETRY,
    RUNTIME_DEADLINE,
    RUNTIME_CAPACITY,
    RUNTIME_FAILURE
} runtime_result_t;

typedef struct {
    crux_mutex_t lock;
    crux_semaphore_t ready;
    crux_semaphore_t gate;
    crux_semaphore_t progress;
    crux_thread_t thread;
    unsigned value;
    int worker_line;
    crux_status_t worker_status;
} runtime_session_t;

typedef struct {
    runtime_session_t *session;
    unsigned increment;
} runtime_task_argument_t;

static runtime_result_t runtime_result(crux_status_t status) {
    switch (status) {
    case CRUX_OK:
        return RUNTIME_OK;
    case CRUX_ERR_BUSY:
        return RUNTIME_RETRY;
    case CRUX_ERR_TIMEOUT:
        return RUNTIME_DEADLINE;
    case CRUX_ERR_NO_RESOURCES:
        return RUNTIME_CAPACITY;
    default:
        return RUNTIME_FAILURE;
    }
}

static crux_thread_options_t runtime_options(size_t stack_words, runtime_priority_t priority) {
    crux_thread_options_t options = {0};
    options.stack_size_bytes = stack_words * sizeof(uint32_t);
    switch (priority) {
    case RUNTIME_BACKGROUND:
        options.priority = CRUX_THREAD_PRIORITY_LOW;
        break;
    case RUNTIME_REGULAR:
        options.priority = CRUX_THREAD_PRIORITY_NORMAL;
        break;
    case RUNTIME_URGENT:
        options.priority = CRUX_THREAD_PRIORITY_HIGH;
        break;
    }
    return options;
}

static int report_failure(int line, const char *operation, crux_status_t got,
                          crux_status_t expected) {
    fprintf(stderr, "runtime fixture line %d: %s: status %d, expected %d\n", line, operation,
            (int)got, (int)expected);
    return line;
}

#define WORKER_EXPECT(operation, expected)                                                         \
    do {                                                                                           \
        crux_status_t actual_status = (operation);                                                 \
        if (actual_status != (expected)) {                                                         \
            session->worker_line = __LINE__;                                                       \
            session->worker_status = actual_status;                                                \
            return;                                                                                \
        }                                                                                          \
    } while (0)

/* Adapts the runtime task argument to Crux's void (*)(void *) entry shape. */
static void runtime_task_entry(void *opaque) {
    runtime_task_argument_t *argument = opaque;
    runtime_session_t *session = argument->session;
    WORKER_EXPECT(crux_semaphore_give(session->ready), CRUX_OK);
    WORKER_EXPECT(crux_semaphore_take(session->gate, WAIT_MS), CRUX_OK);
    /* The owner holds the lock until our progress token is observed. */
    WORKER_EXPECT(crux_mutex_lock(session->lock, 0), CRUX_ERR_BUSY);
    WORKER_EXPECT(crux_mutex_lock(session->lock, EXPIRY_MS), CRUX_ERR_TIMEOUT);
    WORKER_EXPECT(crux_semaphore_give(session->progress), CRUX_OK);
    WORKER_EXPECT(crux_semaphore_take(session->gate, WAIT_MS), CRUX_OK);
    WORKER_EXPECT(crux_mutex_lock(session->lock, WAIT_MS), CRUX_OK);
    session->value += argument->increment;
    WORKER_EXPECT(crux_mutex_unlock(session->lock), CRUX_OK);
    WORKER_EXPECT(crux_semaphore_give(session->progress), CRUX_OK);
}

static int run_session(runtime_priority_t priority, unsigned increment) {
    /* Static storage remains valid even if a broken provider cannot join. */
    static runtime_session_t session;
    static runtime_task_argument_t argument;
    crux_thread_options_t options = runtime_options(512U, priority);
    int failure = 0;
    int have_lock = 0, have_ready = 0, have_gate = 0, have_progress = 0;
    int have_thread = 0, owner_locked = 0;
    session = (runtime_session_t){0};
    argument = (runtime_task_argument_t){&session, increment};

#define EXPECT(operation, expected)                                                                \
    do {                                                                                           \
        crux_status_t actual_status = (operation);                                                 \
        if (actual_status != (expected)) {                                                         \
            failure = report_failure(__LINE__, #operation, actual_status, (expected));             \
            goto cleanup;                                                                          \
        }                                                                                          \
    } while (0)
#define CHECK(condition)                                                                           \
    do {                                                                                           \
        if (!(condition)) {                                                                        \
            fprintf(stderr, "runtime fixture line %d: %s\n", __LINE__, #condition);                \
            failure = __LINE__;                                                                    \
            goto cleanup;                                                                          \
        }                                                                                          \
    } while (0)

    EXPECT(crux_mutex_create(&session.lock), CRUX_OK);
    have_lock = 1;
    EXPECT(crux_semaphore_create(0, 1, &session.ready), CRUX_OK);
    have_ready = 1;
    EXPECT(crux_semaphore_create(0, 1, &session.gate), CRUX_OK);
    have_gate = 1;
    EXPECT(crux_semaphore_create(0, 1, &session.progress), CRUX_OK);
    have_progress = 1;
    CHECK(runtime_result(crux_semaphore_take(session.progress, 0)) == RUNTIME_RETRY);
    CHECK(runtime_result(crux_semaphore_take(session.progress, EXPIRY_MS)) == RUNTIME_DEADLINE);
    EXPECT(crux_semaphore_give(session.progress), CRUX_OK);
    EXPECT(crux_semaphore_give(session.progress), CRUX_ERR_BUSY);
    EXPECT(crux_semaphore_take(session.progress, 0), CRUX_OK);
    EXPECT(crux_semaphore_take(session.progress, 0), CRUX_ERR_BUSY);

    EXPECT(crux_thread_create(&options, runtime_task_entry, &argument, &session.thread), CRUX_OK);
    have_thread = 1;
    EXPECT(crux_semaphore_take(session.ready, WAIT_MS), CRUX_OK);
    CHECK(runtime_result(crux_thread_join(&session.thread, 0)) == RUNTIME_RETRY);
    CHECK(runtime_result(crux_thread_join(&session.thread, EXPIRY_MS)) == RUNTIME_DEADLINE);
    EXPECT(crux_mutex_lock(session.lock, WAIT_MS), CRUX_OK);
    owner_locked = 1;
    EXPECT(crux_semaphore_give(session.gate), CRUX_OK);
    EXPECT(crux_semaphore_take(session.progress, WAIT_MS), CRUX_OK);
    EXPECT(crux_mutex_unlock(session.lock), CRUX_OK);
    owner_locked = 0;
    EXPECT(crux_semaphore_give(session.gate), CRUX_OK);
    EXPECT(crux_semaphore_take(session.progress, WAIT_MS), CRUX_OK);
    /* A completion token does not establish that the worker has exited. */
    EXPECT(crux_thread_join(&session.thread, WAIT_MS), CRUX_OK);
    have_thread = 0;
    CHECK(session.worker_line == 0);
    EXPECT(crux_mutex_lock(session.lock, WAIT_MS), CRUX_OK);
    owner_locked = 1;
    CHECK(session.value == increment);
    EXPECT(crux_mutex_unlock(session.lock), CRUX_OK);
    owner_locked = 0;

cleanup:
    if (owner_locked) {
        crux_status_t status = crux_mutex_unlock(session.lock);
        if (status != CRUX_OK)
            return report_failure(__LINE__, "cleanup unlock", status, CRUX_OK);
    }
    if (have_thread) {
        /* Cooperative release; a timeout never licenses forced reclamation. */
        (void)crux_semaphore_give(session.gate);
        crux_status_t status = crux_thread_join(&session.thread, WAIT_MS);
        if (status != CRUX_OK)
            return report_failure(__LINE__, "cleanup join", status, CRUX_OK);
    }
    if (session.worker_line != 0) {
        fprintf(stderr, "runtime worker line %d: status %d\n", session.worker_line,
                (int)session.worker_status);
        if (failure == 0)
            failure = session.worker_line;
    }
#define DESTROY_IF(owned, operation)                                                               \
    do {                                                                                           \
        if (owned) {                                                                               \
            crux_status_t status = (operation);                                                    \
            if (status != CRUX_OK && failure == 0)                                                 \
                failure = report_failure(__LINE__, #operation, status, CRUX_OK);                   \
        }                                                                                          \
    } while (0)
    DESTROY_IF(have_progress, crux_semaphore_destroy(&session.progress));
    DESTROY_IF(have_gate, crux_semaphore_destroy(&session.gate));
    DESTROY_IF(have_ready, crux_semaphore_destroy(&session.ready));
    DESTROY_IF(have_lock, crux_mutex_destroy(&session.lock));
    return failure;
#undef EXPECT
#undef CHECK
#undef DESTROY_IF
}

typedef struct {
    crux_semaphore_t release;
    crux_status_t wait_status;
} reservation_argument_t;

/* Workers all retain their slots until the owner permits normal return. */
static void reservation_entry(void *opaque) {
    reservation_argument_t *argument = opaque;
    argument->wait_status = crux_semaphore_take(argument->release, WAIT_MS);
}

static int run_reservations(void) {
    static crux_semaphore_t release;
    crux_thread_t tasks[TASK_LIMIT] = {0};
    static reservation_argument_t arguments[TASK_LIMIT];
    crux_thread_options_t options = runtime_options(0, RUNTIME_REGULAR);
    size_t count = 0;
    int failure = 0;
    crux_status_t status = crux_semaphore_create(0, TASK_LIMIT, &release);
    if (status != CRUX_OK)
        return report_failure(__LINE__, "reservation channel", status, CRUX_OK);
    for (; count < TASK_LIMIT; ++count) {
        arguments[count] = (reservation_argument_t){release, CRUX_ERR_INVALID_STATE};
        status = crux_thread_create(&options, reservation_entry, &arguments[count], &tasks[count]);
        if (status != CRUX_OK)
            break;
    }
    if (count == 0 || runtime_result(status) != RUNTIME_CAPACITY) {
        failure =
            report_failure(__LINE__, "bounded task reservations", status, CRUX_ERR_NO_RESOURCES);
    }
    for (size_t index = 0; index < count; ++index) {
        status = crux_semaphore_give(release);
        if (status != CRUX_OK && failure == 0)
            failure = report_failure(__LINE__, "release reservation", status, CRUX_OK);
    }
    int unjoined = 0;
    for (size_t index = 0; index < count; ++index) {
        status = crux_thread_join(&tasks[index], WAIT_MS);
        if (status != CRUX_OK) {
            unjoined = 1;
            if (failure == 0)
                failure = report_failure(__LINE__, "join reservation", status, CRUX_OK);
        } else if (arguments[index].wait_status != CRUX_OK && failure == 0) {
            failure = report_failure(__LINE__, "reservation worker wait",
                                     arguments[index].wait_status, CRUX_OK);
        }
    }
    if (!unjoined) {
        status = crux_semaphore_destroy(&release);
        if (status != CRUX_OK && failure == 0)
            failure = report_failure(__LINE__, "destroy reservation channel", status, CRUX_OK);
    }
    return failure;
}

int crux_runtime_fixture_run(void) {
    for (unsigned repetition = 0; repetition < 4U; ++repetition) {
        int failure = run_reservations();
        if (failure != 0)
            return failure;
        for (unsigned priority = 0; priority < 3U; ++priority) {
            failure = run_session((runtime_priority_t)priority, repetition * 3U + priority + 1U);
            if (failure != 0)
                return failure;
        }
    }
    puts("runtime fixture: PASS (12 sessions, 4 bounded reservation cycles)");
    return 0;
}
