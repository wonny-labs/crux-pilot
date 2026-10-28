#include "sync_internal.h"
#include "thread_internal.h"
#include <stdatomic.h>
#include <stdio.h>

/* Linker wrappers pause immediately after common code admits an operation.
 * These controls are private to tests, never compiled into a provider. */
static atomic_int pause_kind;
static crux_semaphore_t admitted;
static crux_semaphore_t resume_operation;
static int hook_failure;
static sync_native_identity_t test_owner;

static void pause_admitted(int kind)
{
    if (atomic_load(&pause_kind) == 0) {
        return;
    }
    if (crux_sync_identity_equal(test_owner, crux_sync_current_identity())) {
        return;
    }
    int expected = kind;
    if (atomic_compare_exchange_strong(&pause_kind, &expected, 0)) {
        if (crux_semaphore_give(admitted) != CRUX_OK ||
            crux_semaphore_take(resume_operation, 2000) != CRUX_OK) {
            hook_failure = 1;
        }
    }
}
/* GNU ld --wrap requires these reserved linker symbol names. */
// NOLINTBEGIN(bugprone-reserved-identifier)
crux_status_t __real_crux_thread_impl_join(thread_native_thread_t *,
                                               const thread_native_deadline_t *);
crux_status_t __real_crux_mutex_impl_lock(sync_native_mutex_t *, const sync_native_deadline_t *);
crux_status_t __real_crux_semaphore_impl_take(sync_native_semaphore_t *,
                                                const sync_native_deadline_t *);
crux_status_t __wrap_crux_thread_impl_join(thread_native_thread_t *object,
                                               const thread_native_deadline_t *deadline)
{
    pause_admitted(1);
    return __real_crux_thread_impl_join(object, deadline);
}
crux_status_t __wrap_crux_mutex_impl_lock(sync_native_mutex_t *object,
                                            const sync_native_deadline_t *deadline)
{
    pause_admitted(2);
    return __real_crux_mutex_impl_lock(object, deadline);
}
crux_status_t __wrap_crux_semaphore_impl_take(sync_native_semaphore_t *object,
                                                const sync_native_deadline_t *deadline)
{
    pause_admitted(3);
    return __real_crux_semaphore_impl_take(object, deadline);
}

// NOLINTEND(bugprone-reserved-identifier)

#define REQUIRE(condition)                                                                         \
    do {                                                                                           \
        if (!(condition)) {                                                                        \
            printf("admission failure line=%d\n", __LINE__);                                       \
            return __LINE__;                                                                       \
        }                                                                                          \
    } while (0)
static const crux_thread_options_t options = {.priority = CRUX_THREAD_PRIORITY_NORMAL};
typedef struct {
    crux_thread_t thread;
    crux_mutex_t mutex;
    crux_semaphore_t semaphore;
    crux_status_t result;
} context_t;
static void join_worker(void *argument)
{
    context_t *context = argument;
    context->result = crux_thread_join(&context->thread, 1500);
}
static void take_worker(void *argument)
{
    context_t *context = argument;
    context->result = crux_semaphore_take(context->semaphore, 1500);
}
static void lock_worker(void *argument)
{
    context_t *context = argument;
    context->result = crux_mutex_lock(context->mutex, 1500);
    if (context->result == CRUX_OK) {
        context->result = crux_mutex_unlock(context->mutex);
    }
}
int execution_admission_cases(void)
{
    test_owner = crux_sync_current_identity();
    REQUIRE(crux_semaphore_create(0, 1, &admitted) == CRUX_OK);
    REQUIRE(crux_semaphore_create(0, 1, &resume_operation) == CRUX_OK);
    context_t target = {0};
    REQUIRE(crux_semaphore_create(0, 1, &target.semaphore) == CRUX_OK);
    REQUIRE(crux_thread_create(&options, take_worker, &target, &target.thread) == CRUX_OK);
    context_t joiner = {.thread = target.thread};
    crux_thread_t joining_thread;
    atomic_store(&pause_kind, 1);
    REQUIRE(crux_thread_create(&options, join_worker, &joiner, &joining_thread) == CRUX_OK);
    REQUIRE(crux_semaphore_take(admitted, 1000) == CRUX_OK);
    REQUIRE(crux_thread_join(&target.thread, 0) == CRUX_ERR_BUSY);
    REQUIRE(crux_semaphore_give(target.semaphore) == CRUX_OK);
    REQUIRE(crux_semaphore_give(resume_operation) == CRUX_OK);
    REQUIRE(crux_thread_join(&joining_thread, 2000) == CRUX_OK);
    REQUIRE(joiner.result == CRUX_OK && target.result == CRUX_OK);
    REQUIRE(crux_thread_join(&target.thread, 0) == CRUX_ERR_INVALID_ARGUMENT);
    REQUIRE(crux_semaphore_destroy(&target.semaphore) == CRUX_OK);

    REQUIRE(crux_semaphore_create(0, 1, &target.semaphore) == CRUX_OK);
    atomic_store(&pause_kind, 3);
    REQUIRE(crux_thread_create(&options, take_worker, &target, &target.thread) == CRUX_OK);
    REQUIRE(crux_semaphore_take(admitted, 1000) == CRUX_OK);
    REQUIRE(crux_semaphore_destroy(&target.semaphore) == CRUX_ERR_BUSY);
    REQUIRE(crux_semaphore_give(target.semaphore) == CRUX_OK);
    REQUIRE(crux_semaphore_give(resume_operation) == CRUX_OK);
    REQUIRE(crux_thread_join(&target.thread, 2000) == CRUX_OK);
    REQUIRE(target.result == CRUX_OK);
    REQUIRE(crux_semaphore_destroy(&target.semaphore) == CRUX_OK);

    REQUIRE(crux_mutex_create(&target.mutex) == CRUX_OK);
    REQUIRE(crux_mutex_lock(target.mutex, 0) == CRUX_OK);
    atomic_store(&pause_kind, 2);
    REQUIRE(crux_thread_create(&options, lock_worker, &target, &target.thread) == CRUX_OK);
    REQUIRE(crux_semaphore_take(admitted, 1000) == CRUX_OK);
    REQUIRE(crux_mutex_unlock(target.mutex) == CRUX_OK);
    REQUIRE(crux_mutex_destroy(&target.mutex) == CRUX_ERR_BUSY);
    REQUIRE(crux_semaphore_give(resume_operation) == CRUX_OK);
    REQUIRE(crux_thread_join(&target.thread, 2000) == CRUX_OK);
    REQUIRE(target.result == CRUX_OK);
    REQUIRE(crux_mutex_destroy(&target.mutex) == CRUX_OK);

    REQUIRE(hook_failure == 0);
    REQUIRE(crux_semaphore_destroy(&admitted) == CRUX_OK);
    REQUIRE(crux_semaphore_destroy(&resume_operation) == CRUX_OK);
    return 0;
}
