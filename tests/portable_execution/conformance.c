#include <crux/execution.h>
#include <limits.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#define CHECK(condition)                                                                           \
    do {                                                                                           \
        if (!(condition)) {                                                                        \
            printf("FAIL %s:%d: %s\n", __func__, __LINE__, #condition);                            \
            return __LINE__;                                                                       \
        }                                                                                          \
    } while (0)

static const crux_thread_options_t options = {.stack_size_bytes = 4096,
                                              .priority = CRUX_THREAD_PRIORITY_NORMAL};

uint64_t execution_test_now_ns(void);

static void publish(void *argument) {
    int *value = argument;
    *value = 42;
}

int execution_thread_lifetime(void) {
    crux_thread_t thread = {0};
    int value = 0;
    CHECK(crux_thread_create(&options, publish, &value, &thread) == CRUX_OK);
    crux_thread_t alias = thread;
    CHECK(crux_thread_join(&thread, 1000) == CRUX_OK);
    CHECK(value == 42);
    CHECK(crux_thread_join(&thread, 0) == CRUX_ERR_INVALID_ARGUMENT);
    CHECK(crux_thread_join(&alias, 0) == CRUX_ERR_INVALID_ARGUMENT);
    value = 0;
    CHECK(crux_thread_create(&options, publish, &value, &thread) == CRUX_OK);
    CHECK(crux_thread_join(&alias, 0) == CRUX_ERR_INVALID_ARGUMENT);
    CHECK(crux_thread_join(&thread, 1000) == CRUX_OK);
    CHECK(value == 42);
    return 0;
}

int execution_thread_inputs(void) {
    crux_thread_t thread = {0};
    int value = 0;
    CHECK(crux_thread_create(NULL, publish, &value, &thread) == CRUX_ERR_INVALID_ARGUMENT);
    CHECK(crux_thread_create(&options, NULL, &value, &thread) == CRUX_ERR_INVALID_ARGUMENT);
    CHECK(crux_thread_create(&options, publish, &value, NULL) == CRUX_ERR_INVALID_ARGUMENT);
    CHECK(crux_thread_join(NULL, 0) == CRUX_ERR_INVALID_ARGUMENT);
    crux_thread_options_t invalid = options;
    /* Deliberately invalid enum inputs exercise public argument validation. */
    // NOLINTNEXTLINE(clang-analyzer-optin.core.EnumCastOutOfRange)
    invalid.priority = (crux_thread_priority_t)99;
    CHECK(crux_thread_create(&invalid, publish, &value, &thread) == CRUX_ERR_INVALID_ARGUMENT);
    /* Deliberately invalid enum inputs exercise public argument validation. */
    // NOLINTNEXTLINE(clang-analyzer-optin.core.EnumCastOutOfRange)
    invalid.priority = (crux_thread_priority_t)-1;
    CHECK(crux_thread_create(&invalid, publish, &value, &thread) == CRUX_ERR_INVALID_ARGUMENT);
    invalid = options;
    invalid.stack_size_bytes = SIZE_MAX;
    CHECK(crux_thread_create(&invalid, publish, &value, &thread) == CRUX_ERR_OUT_OF_RANGE);
    CHECK(crux_thread_create(&options, publish, &value, &thread) == CRUX_OK);
    CHECK(crux_thread_join(&thread, UINT32_MAX) == CRUX_ERR_OUT_OF_RANGE);
    CHECK(crux_thread_join(&thread, 1000) == CRUX_OK);
    return 0;
}

int execution_thread_capacity(void) {
    crux_thread_t threads[4];
    int values[4] = {0};
    for (size_t i = 0; i < 4; ++i) {
        CHECK(crux_thread_create(&options, publish, &values[i], &threads[i]) == CRUX_OK);
    }
    crux_thread_t extra = {0};
    CHECK(crux_thread_create(&options, publish, &values[0], &extra) == CRUX_ERR_NO_RESOURCES);
    for (size_t i = 0; i < 4; ++i) {
        CHECK(crux_thread_join(&threads[i], 1000) == CRUX_OK);
        CHECK(values[i] == 42);
    }
    CHECK(crux_thread_create(&options, publish, &values[0], &extra) == CRUX_OK);
    CHECK(crux_thread_join(&extra, 1000) == CRUX_OK);
    return 0;
}

typedef struct {
    crux_mutex_t mutex;
    crux_semaphore_t gate;
    crux_status_t lock_result;
    crux_status_t unlock_result;
} mutex_context_t;

static void wrong_owner(void *argument) {
    mutex_context_t *context = argument;
    context->unlock_result = crux_mutex_unlock(context->mutex);
    context->lock_result = crux_mutex_lock(context->mutex, 20);
}

static void acquire_after_release(void *argument) {
    mutex_context_t *context = argument;
    context->lock_result = crux_semaphore_give(context->gate);
    if (context->lock_result == CRUX_OK) {
        context->lock_result = crux_mutex_lock(context->mutex, 1000);
    }
    if (context->lock_result == CRUX_OK) {
        context->unlock_result = crux_mutex_unlock(context->mutex);
    }
}

int execution_mutex_ownership(void) {
    crux_mutex_t mutex = {0};
    CHECK(crux_mutex_create(&mutex) == CRUX_OK);
    CHECK(crux_mutex_unlock(mutex) == CRUX_ERR_INVALID_STATE);
    CHECK(crux_mutex_lock(mutex, UINT32_MAX) == CRUX_ERR_OUT_OF_RANGE);
    CHECK(crux_mutex_lock(mutex, 0) == CRUX_OK);
    CHECK(crux_mutex_lock(mutex, 0) == CRUX_ERR_INVALID_STATE);
    CHECK(crux_mutex_destroy(&mutex) == CRUX_ERR_BUSY);
    mutex_context_t context = {.mutex = mutex};
    crux_thread_t worker;
    uint64_t started = execution_test_now_ns();
    CHECK(crux_thread_create(&options, wrong_owner, &context, &worker) == CRUX_OK);
    CHECK(crux_thread_join(&worker, 1000) == CRUX_OK);
    CHECK(context.unlock_result == CRUX_ERR_INVALID_STATE);
    CHECK(context.lock_result == CRUX_ERR_TIMEOUT);
    CHECK(execution_test_now_ns() - started >= UINT64_C(20000000));
    CHECK(crux_mutex_unlock(mutex) == CRUX_OK);
    crux_mutex_t alias = mutex;
    CHECK(crux_mutex_destroy(&mutex) == CRUX_OK);
    CHECK(crux_mutex_lock(alias, 0) == CRUX_ERR_INVALID_ARGUMENT);
    CHECK(crux_mutex_create(&mutex) == CRUX_OK);
    CHECK(crux_mutex_lock(alias, 0) == CRUX_ERR_INVALID_ARGUMENT);
    CHECK(crux_mutex_destroy(&mutex) == CRUX_OK);
    return 0;
}

int execution_mutex_wakeup(void) {
    mutex_context_t context = {0};
    CHECK(crux_mutex_create(&context.mutex) == CRUX_OK);
    CHECK(crux_semaphore_create(0, 1, &context.gate) == CRUX_OK);
    CHECK(crux_mutex_lock(context.mutex, 0) == CRUX_OK);
    crux_thread_t worker;
    CHECK(crux_thread_create(&options, acquire_after_release, &context, &worker) == CRUX_OK);
    CHECK(crux_semaphore_take(context.gate, 1000) == CRUX_OK);
    CHECK(crux_mutex_unlock(context.mutex) == CRUX_OK);
    CHECK(crux_thread_join(&worker, 1000) == CRUX_OK);
    CHECK(context.lock_result == CRUX_OK && context.unlock_result == CRUX_OK);
    CHECK(crux_mutex_destroy(&context.mutex) == CRUX_OK);
    CHECK(crux_semaphore_destroy(&context.gate) == CRUX_OK);
    return 0;
}

int execution_semaphore_tokens(void) {
    crux_semaphore_t semaphore = {0};
    CHECK(crux_semaphore_create(0, 0, &semaphore) == CRUX_ERR_OUT_OF_RANGE);
    CHECK(crux_semaphore_create(2, 1, &semaphore) == CRUX_ERR_OUT_OF_RANGE);
    CHECK(crux_semaphore_create(0, UINT32_MAX, &semaphore) == CRUX_ERR_OUT_OF_RANGE);
    CHECK(crux_semaphore_create(0, 2, &semaphore) == CRUX_OK);
    CHECK(crux_semaphore_take(semaphore, 0) == CRUX_ERR_BUSY);
    uint64_t started = execution_test_now_ns();
    CHECK(crux_semaphore_take(semaphore, 10) == CRUX_ERR_TIMEOUT);
    CHECK(execution_test_now_ns() - started >= UINT64_C(10000000));
    CHECK(crux_semaphore_take(semaphore, UINT32_MAX) == CRUX_ERR_OUT_OF_RANGE);
    CHECK(crux_semaphore_give(semaphore) == CRUX_OK);
    CHECK(crux_semaphore_give(semaphore) == CRUX_OK);
    CHECK(crux_semaphore_give(semaphore) == CRUX_ERR_BUSY);
    CHECK(crux_semaphore_take(semaphore, 0) == CRUX_OK);
    CHECK(crux_semaphore_take(semaphore, 0) == CRUX_OK);
    CHECK(crux_semaphore_take(semaphore, 0) == CRUX_ERR_BUSY);
    crux_semaphore_t alias = semaphore;
    CHECK(crux_semaphore_destroy(&semaphore) == CRUX_OK);
    CHECK(crux_semaphore_give(alias) == CRUX_ERR_INVALID_ARGUMENT);
    CHECK(crux_semaphore_create(0, 1, &semaphore) == CRUX_OK);
    CHECK(crux_semaphore_give(alias) == CRUX_ERR_INVALID_ARGUMENT);
    CHECK(crux_semaphore_destroy(&semaphore) == CRUX_OK);
    return 0;
}

int execution_sync_capacity(void) {
    crux_mutex_t mutexes[8];
    crux_semaphore_t semaphores[8];
    for (size_t i = 0; i < 8; ++i) {
        CHECK(crux_mutex_create(&mutexes[i]) == CRUX_OK);
        CHECK(crux_semaphore_create(0, 1, &semaphores[i]) == CRUX_OK);
    }
    crux_mutex_t extra_mutex = {0};
    crux_semaphore_t extra_semaphore = {0};
    CHECK(crux_mutex_create(&extra_mutex) == CRUX_ERR_NO_RESOURCES);
    CHECK(crux_semaphore_create(0, 1, &extra_semaphore) == CRUX_ERR_NO_RESOURCES);
    for (size_t i = 0; i < 8; ++i) {
        CHECK(crux_mutex_destroy(&mutexes[i]) == CRUX_OK);
        CHECK(crux_semaphore_destroy(&semaphores[i]) == CRUX_OK);
    }
    CHECK(crux_mutex_create(&extra_mutex) == CRUX_OK);
    CHECK(crux_semaphore_create(0, 1, &extra_semaphore) == CRUX_OK);
    CHECK(crux_mutex_destroy(&extra_mutex) == CRUX_OK);
    CHECK(crux_semaphore_destroy(&extra_semaphore) == CRUX_OK);
    CHECK(crux_mutex_create(NULL) == CRUX_ERR_INVALID_ARGUMENT);
    CHECK(crux_semaphore_create(0, 1, NULL) == CRUX_ERR_INVALID_ARGUMENT);
    CHECK(crux_mutex_destroy(NULL) == CRUX_ERR_INVALID_ARGUMENT);
    CHECK(crux_semaphore_destroy(NULL) == CRUX_ERR_INVALID_ARGUMENT);
    CHECK(crux_mutex_unlock((crux_mutex_t){0}) == CRUX_ERR_INVALID_ARGUMENT);
    CHECK(crux_semaphore_take((crux_semaphore_t){0}, 0) == CRUX_ERR_INVALID_ARGUMENT);
    return 0;
}

typedef struct {
    crux_semaphore_t gate;
    crux_thread_t self;
    crux_status_t result;
    int published;
} gate_context_t;

static void gated_worker(void *argument) {
    gate_context_t *context = argument;
    context->result = crux_semaphore_take(context->gate, 2000);
    if (context->result == CRUX_OK) {
        context->published = 73;
    }
}

static void self_join_worker(void *argument) {
    gate_context_t *context = argument;
    context->result = crux_semaphore_take(context->gate, 2000);
    if (context->result == CRUX_OK) {
        context->result = crux_thread_join(&context->self, 0);
    }
}

int execution_join_timeout_retains(void) {
    gate_context_t context = {0};
    CHECK(crux_semaphore_create(0, 1, &context.gate) == CRUX_OK);
    crux_thread_t worker;
    CHECK(crux_thread_create(&options, gated_worker, &context, &worker) == CRUX_OK);
    CHECK(crux_thread_join(&worker, 0) == CRUX_ERR_BUSY);
    uint64_t started = execution_test_now_ns();
    CHECK(crux_thread_join(&worker, 10) == CRUX_ERR_TIMEOUT);
    CHECK(execution_test_now_ns() - started >= UINT64_C(10000000));
    crux_thread_t others[3];
    int values[3] = {0};
    for (size_t i = 0; i < 3; ++i) {
        CHECK(crux_thread_create(&options, publish, &values[i], &others[i]) == CRUX_OK);
    }
    crux_thread_t extra;
    CHECK(crux_thread_create(&options, publish, &values[0], &extra) == CRUX_ERR_NO_RESOURCES);
    CHECK(crux_semaphore_give(context.gate) == CRUX_OK);
    CHECK(crux_thread_join(&worker, 1000) == CRUX_OK);
    CHECK(context.result == CRUX_OK && context.published == 73);
    for (size_t i = 0; i < 3; ++i) {
        CHECK(crux_thread_join(&others[i], 1000) == CRUX_OK);
    }
    CHECK(crux_semaphore_destroy(&context.gate) == CRUX_OK);
    CHECK(crux_semaphore_create(0, 1, &context.gate) == CRUX_OK);
    CHECK(crux_thread_create(&options, self_join_worker, &context, &worker) == CRUX_OK);
    context.self = worker;
    CHECK(crux_semaphore_give(context.gate) == CRUX_OK);
    CHECK(crux_thread_join(&worker, 1000) == CRUX_OK);
    CHECK(context.result == CRUX_ERR_INVALID_STATE);
    CHECK(crux_semaphore_destroy(&context.gate) == CRUX_OK);
    return 0;
}

typedef struct {
    crux_semaphore_t gate;
    crux_semaphore_t tokens;
    crux_status_t result;
} giver_context_t;

static void give_once(void *argument) {
    giver_context_t *context = argument;
    context->result = crux_semaphore_take(context->gate, 1000);
    if (context->result == CRUX_OK) {
        context->result = crux_semaphore_give(context->tokens);
    }
}

int execution_concurrent_give(void) {
    giver_context_t contexts[2] = {0};
    CHECK(crux_semaphore_create(0, 2, &contexts[0].gate) == CRUX_OK);
    CHECK(crux_semaphore_create(0, 1, &contexts[0].tokens) == CRUX_OK);
    contexts[1] = contexts[0];
    crux_thread_t workers[2];
    for (size_t i = 0; i < 2; ++i) {
        CHECK(crux_thread_create(&options, give_once, &contexts[i], &workers[i]) == CRUX_OK);
    }
    CHECK(crux_semaphore_give(contexts[0].gate) == CRUX_OK);
    CHECK(crux_semaphore_give(contexts[0].gate) == CRUX_OK);
    for (size_t i = 0; i < 2; ++i) {
        CHECK(crux_thread_join(&workers[i], 1000) == CRUX_OK);
    }
    CHECK((contexts[0].result == CRUX_OK && contexts[1].result == CRUX_ERR_BUSY) ||
          (contexts[1].result == CRUX_OK && contexts[0].result == CRUX_ERR_BUSY));
    CHECK(crux_semaphore_take(contexts[0].tokens, 0) == CRUX_OK);
    CHECK(crux_semaphore_take(contexts[0].tokens, 0) == CRUX_ERR_BUSY);
    CHECK(crux_semaphore_destroy(&contexts[0].tokens) == CRUX_OK);
    CHECK(crux_semaphore_destroy(&contexts[0].gate) == CRUX_OK);
    return 0;
}
