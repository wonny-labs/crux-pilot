/* Emitting an event never waits on storage or on the log path. Real threads, with the storage fake
 * and the log fake both blocked. A wait here would show as a join that times out. */
#include "joined.h"

/* The hooks and workers run on real threads, and this one counts atomically. */
#undef CHECK
#define CHECK(expression) EVENT_CHECK(expression)

#define JOIN_MS 30000u

#define RING ((size_t)CRUX_SECURITY_EVENT_RING)

static crux_semaphore_t log_entered, log_release;
static crux_semaphore_t save_entered, save_release;
static atomic_bool log_blocks;

static void hook_block_log(void *context)
{
    (void)context;
    if (atomic_exchange(&log_blocks, false)) {
        CHECK(crux_semaphore_give(log_entered) == CRUX_OK);
        CHECK(crux_semaphore_take(log_release, JOIN_MS) == CRUX_OK);
    }
}

static void hook_block_save(void *context)
{
    (void)context;
    CHECK(crux_semaphore_give(save_entered) == CRUX_OK);
    CHECK(crux_semaphore_take(save_release, JOIN_MS) == CRUX_OK);
}

static void slow_change(void *argument)
{
    crux_access_session_t admin = *(crux_access_session_t *)argument;
    CHECK(crux_access_account_create(admin, "bob", "viewer", ACCESS_TEST_PASSWORD,
                                     strlen(ACCESS_TEST_PASSWORD)) == CRUX_ACCESS_OK);
}

typedef struct {
    crux_access_session_t user;
    size_t emitted;
} emitter_t;

/* What must not wait: a denial through access control, then a flood past the ring's size. */
static void emit_while_blocked(void *argument)
{
    emitter_t *emitter = argument;
    CHECK(crux_access_check(emitter->user, CRUX_ACCESS_PERMISSION_MANAGE) ==
          CRUX_ACCESS_ERR_DENIED);
    emitter->emitted += 1;
    for (size_t index = 0; index < 3u * RING; ++index) {
        event_test_emit_routine();
        emitter->emitted += 1;
    }
    event_test_emit_change();
    emitter->emitted += 1;
}

static void never_waits_on_storage_or_log(void)
{
    crux_access_session_t admin = joined_boot_admin();
    access_test_create_user(admin);
    crux_access_session_t user =
        access_test_session(ACCESS_TEST_USER, ACCESS_TEST_PASSWORD, &access_test_web_other);
    event_test_drain_all();
    event_test_lines_clear();
    const crux_security_event_stats_t start = event_test_stats();
    event_test_clock_real(true);

    CHECK(crux_semaphore_create(0, 1, &log_entered) == CRUX_OK);
    CHECK(crux_semaphore_create(0, 1, &log_release) == CRUX_OK);
    CHECK(crux_semaphore_create(0, 1, &save_entered) == CRUX_OK);
    CHECK(crux_semaphore_create(0, 1, &save_release) == CRUX_OK);

    /* The drain's thread stops inside a log write. */
    atomic_store(&log_blocks, true);
    event_test_log_hook(hook_block_log, NULL);
    CHECK(crux_security_event_start() == CRUX_OK);
    event_test_emit_routine();
    CHECK(crux_semaphore_take(log_entered, JOIN_MS) == CRUX_OK);

    /* A change stops inside its save, holding the change lock. */
    access_fake_store_save_hook(hook_block_save, NULL);
    const crux_thread_options_t options = {0, CRUX_THREAD_PRIORITY_NORMAL};
    crux_thread_t saver;
    CHECK(crux_thread_create(&options, slow_change, &admin, &saver) == CRUX_OK);
    CHECK(crux_semaphore_take(save_entered, JOIN_MS) == CRUX_OK);

    /* An emit still returns: the ring lock has no holder across either. */
    emitter_t emitter = {user, 0};
    crux_thread_t worker;
    CHECK(crux_thread_create(&options, emit_while_blocked, &emitter, &worker) == CRUX_OK);
    CHECK(crux_thread_join(&worker, JOIN_MS) == CRUX_OK);
    CHECK(emitter.emitted == 3u * RING + 2u);

    /* The drain took one record out before it stopped, so the ring filled behind it. */
    crux_security_event_stats_t blocked = event_test_stats();
    CHECK(blocked.emitted - start.emitted >= RING - CRUX_SECURITY_EVENT_RESERVE);
    CHECK(blocked.dropped > start.dropped);
    CHECK(blocked.high_water <= RING);
    CHECK(blocked.delivered == start.delivered);

    /* The record of the denial is there. */
    uint32_t cursor = 0;
    crux_security_event_record_t record;
    bool found = false;
    while (crux_security_event_test_read(&cursor, &record) == CRUX_SECURITY_READ_OK) {
        found = found || record.id == CRUX_SECURITY_EVENT_ID_ACCESS_PERMISSION_DENIED;
    }
    CHECK(found);

    /* A check also returns under a blocked save: it never takes the change lock. */
    CHECK(crux_access_check(user, "status.read") == CRUX_ACCESS_OK);

    CHECK(crux_semaphore_give(save_release) == CRUX_OK);
    CHECK(crux_thread_join(&saver, JOIN_MS) == CRUX_OK);
    CHECK(crux_semaphore_give(log_release) == CRUX_OK);

    /* Everything drains, with no wait left over. */
    uint32_t expected_emitted = 0;
    for (unsigned attempt = 0; attempt < 3000; ++attempt) {
        expected_emitted = event_test_stats().emitted;
        if (crux_security_event_test_pending() == 0 &&
            event_test_stats().delivered - start.delivered >= expected_emitted - start.emitted) {
            break;
        }
        CHECK(crux_thread_sleep_for(crux_duration_from_ms(10)) == CRUX_OK);
    }
    CHECK(crux_security_event_test_pending() == 0);
    CHECK(crux_security_event_test_stop_drain() == CRUX_OK);
    event_test_log_hook(NULL, NULL);

    CHECK(crux_semaphore_destroy(&log_entered) == CRUX_OK);
    CHECK(crux_semaphore_destroy(&log_release) == CRUX_OK);
    CHECK(crux_semaphore_destroy(&save_entered) == CRUX_OK);
    CHECK(crux_semaphore_destroy(&save_release) == CRUX_OK);
}

/* The contract allows an emit with the state lock held, as a defence. Nothing it calls takes it. */
static void emit_under_the_state_lock_returns(void)
{
    (void)joined_boot_admin();
    event_test_drain_all();
    uint32_t emitted = event_test_stats().emitted;
    CHECK(crux_access_lock_state());
    event_test_emit_routine();
    crux_access_unlock_state();
    CHECK(event_test_stats().emitted == emitted + 1u);
    event_test_drain_all();
}

int main(void)
{
    never_waits_on_storage_or_log();
    emit_under_the_state_lock_returns();
    int access_status = access_test_finish("Security event waits");
    int event_status = event_test_finish("Security event waits, support");
    return access_status != 0 || event_status != 0 ? 1 : 0;
}
