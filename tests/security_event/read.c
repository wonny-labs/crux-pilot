/* crux_security_event_read with the real access control: what a session must hold, and that the
 * permission check runs before the ring lock. */
#include "joined.h"

static const char *const reader_permissions[] = {CRUX_SECURITY_EVENT_PERMISSION_READ};
static const char *const viewer_permissions[] = {"status.read"};
static const char *const administrator_permissions[] = {CRUX_ACCESS_PERMISSION_MANAGE};
static const crux_access_role_t roles[] = {
    {"reader", reader_permissions, 1},
    {"viewer", viewer_permissions, 1},
    {"administrator", administrator_permissions, 1},
};
static const crux_access_policy_t policy = {roles, 3, 900, 28800, NULL};

// NOLINTNEXTLINE(bugprone-reserved-identifier)
crux_access_result_t __real_crux_access_check(crux_access_session_t session,
                                              const char *permission);
// NOLINTNEXTLINE(bugprone-reserved-identifier)
crux_access_result_t __wrap_crux_access_check(crux_access_session_t session,
                                              const char *permission);

static unsigned checks_seen;

/* The denial's own emit takes the ring lock, so the check must never run under it. */
// NOLINTNEXTLINE(bugprone-reserved-identifier)
crux_access_result_t __wrap_crux_access_check(crux_access_session_t session, const char *permission)
{
    checks_seen += 1;
    event_test_expect_ring_lock_free(NULL);
    return __real_crux_access_check(session, permission);
}

typedef struct {
    crux_access_session_t admin;
    crux_access_session_t reader;
    crux_access_session_t viewer;
} sessions_t;

static sessions_t boot(void)
{
    event_test_boot();
    access_test_boot(&policy);
    const size_t length = strlen(ACCESS_TEST_PASSWORD);
    CHECK(crux_access_provision_first_account("admin", ACCESS_TEST_PASSWORD, length,
                                              "administrator",
                                              &access_test_serial) == CRUX_ACCESS_OK);
    sessions_t sessions;
    sessions.admin = access_test_session("admin", ACCESS_TEST_PASSWORD, &access_test_web);
    CHECK(crux_access_account_create(sessions.admin, "rita", "reader", ACCESS_TEST_PASSWORD,
                                     length) == CRUX_ACCESS_OK);
    CHECK(crux_access_account_create(sessions.admin, "vic", "viewer", ACCESS_TEST_PASSWORD,
                                     length) == CRUX_ACCESS_OK);
    sessions.reader = access_test_session("rita", ACCESS_TEST_PASSWORD, &access_test_web);
    sessions.viewer = access_test_session("vic", ACCESS_TEST_PASSWORD, &access_test_web_other);
    return sessions;
}

/* The newest record in the ring. */
static crux_security_event_record_t newest(void)
{
    uint32_t cursor = 0;
    crux_security_event_record_t record;
    memset(&record, 0, sizeof(record));
    crux_security_event_record_t next;
    while (crux_security_event_read_unchecked(&cursor, &next) == CRUX_SECURITY_READ_OK) {
        record = next;
    }
    return record;
}

static void refused_sessions(void)
{
    sessions_t sessions = boot();
    uint32_t cursor = 0;
    crux_security_event_record_t record;
    event_test_record_fill(&record);

    /* No session. */
    const crux_access_session_t none = {0};
    CHECK(crux_security_event_read(none, &cursor, &record) ==
          CRUX_SECURITY_READ_ERR_SESSION_INVALID);
    CHECK(newest().id == CRUX_SECURITY_EVENT_ID_ACCESS_SESSION_INVALID);

    /* No events.read: the denial is itself recorded, naming the permission. */
    CHECK(crux_security_event_read(sessions.viewer, &cursor, &record) ==
          CRUX_SECURITY_READ_ERR_DENIED);
    crux_security_event_record_t denial = newest();
    CHECK(denial.id == CRUX_SECURITY_EVENT_ID_ACCESS_PERMISSION_DENIED);
    CHECK(strcmp(denial.user, "vic") == 0);
    CHECK(strcmp(denial.subject, "events.read") == 0);

    /* An administrator has only what its role lists. */
    CHECK(crux_security_event_read(sessions.admin, &cursor, &record) ==
          CRUX_SECURITY_READ_ERR_DENIED);

    /* An ended session. */
    crux_access_session_t ended = sessions.viewer;
    crux_access_session_t logout = sessions.viewer;
    crux_access_logout(&logout);
    CHECK(crux_security_event_read(ended, &cursor, &record) ==
          CRUX_SECURITY_READ_ERR_SESSION_INVALID);

    /* An expired session. */
    access_fake_clock_advance((uint64_t)policy.session_idle_limit_s * 1000u);
    CHECK(crux_security_event_read(sessions.reader, &cursor, &record) ==
          CRUX_SECURITY_READ_ERR_SESSION_INVALID);
    CHECK(newest().id == CRUX_SECURITY_EVENT_ID_ACCESS_SESSION_EXPIRED);

    CHECK(cursor == 0);
    CHECK(event_test_record_untouched(&record));
}

static void forced_change_cannot_read(void)
{
    sessions_t sessions = boot();
    CHECK(crux_access_account_reset_password(sessions.admin, "rita", ACCESS_TEST_OTHER_PASSWORD,
                                             strlen(ACCESS_TEST_OTHER_PASSWORD)) == CRUX_ACCESS_OK);
    crux_access_session_t forced =
        access_test_session("rita", ACCESS_TEST_OTHER_PASSWORD, &access_test_web);
    uint32_t cursor = 0;
    crux_security_event_record_t record;
    CHECK(crux_security_event_read(forced, &cursor, &record) == CRUX_SECURITY_READ_ERR_DENIED);
}

static void permitted_walks_with_a_gap(void)
{
    sessions_t sessions = boot();
    /* Boot already recorded the account changes and logins, and drained none of them. */
    uint32_t cursor = 0;
    crux_security_event_record_t record;
    CHECK(crux_security_event_read(sessions.reader, &cursor, &record) == CRUX_SECURITY_READ_OK);
    CHECK(record.id == CRUX_SECURITY_EVENT_ID_ACCESS_PROVISIONED);
    CHECK(record.sequence == 1);
    CHECK(strcmp(record.user, "admin") == 0);
    CHECK(record.channel == 1);
    CHECK(cursor == 2);
    while (crux_security_event_read(sessions.reader, &cursor, &record) == CRUX_SECURITY_READ_OK) {
    }
    event_test_drain_all();

    /* Overwrite the oldest with new records: the old cursor sees a gap, not an error. */
    uint32_t old_cursor = 2;
    for (size_t index = 0; index < 2u * (size_t)CRUX_SECURITY_EVENT_RING; ++index) {
        event_test_emit_routine();
        event_test_drain_all();
    }
    CHECK(crux_security_event_read(sessions.reader, &old_cursor, &record) == CRUX_SECURITY_READ_OK);
    CHECK(record.sequence > 2u);
    CHECK(record.id == CRUX_SECURITY_EVENT_ID_ACCESS_LOGIN_FAILED);
}

static void read_does_not_extend_a_session(void)
{
    sessions_t sessions = boot();
    uint32_t idle_ms = policy.session_idle_limit_s * 1000u;
    access_fake_clock_advance(idle_ms - 1u);
    uint32_t cursor = 0;
    crux_security_event_record_t record;
    CHECK(crux_security_event_read(sessions.reader, &cursor, &record) == CRUX_SECURITY_READ_OK);
    access_fake_clock_advance(1);
    CHECK(crux_security_event_read(sessions.reader, &cursor, &record) ==
          CRUX_SECURITY_READ_ERR_SESSION_INVALID);
}

static void arguments_before_the_check(void)
{
    sessions_t sessions = boot();
    unsigned before = checks_seen;
    uint32_t cursor = 0;
    crux_security_event_record_t record;
    CHECK(crux_security_event_read(sessions.viewer, NULL, &record) ==
          CRUX_SECURITY_READ_ERR_INVALID_ARGUMENT);
    CHECK(crux_security_event_read(sessions.viewer, &cursor, NULL) ==
          CRUX_SECURITY_READ_ERR_INVALID_ARGUMENT);
    CHECK(checks_seen == before);
    CHECK(crux_security_event_read(sessions.reader, &cursor, &record) == CRUX_SECURITY_READ_OK);
    CHECK(checks_seen == before + 1u);
}

int main(void)
{
    refused_sessions();
    forced_change_cannot_read();
    permitted_walks_with_a_gap();
    read_does_not_extend_a_session();
    arguments_before_the_check();
    int access_status = access_test_finish("Security event read");
    int event_status = event_test_finish("Security event read, support");
    return access_status != 0 || event_status != 0 ? 1 : 0;
}
