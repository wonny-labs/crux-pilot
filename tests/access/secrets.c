/* Known passwords, keys and verifier values must not appear in any recorded event or log, over a
 * flow that reaches every event type. */
#include "access_test.h"

#define PASSWORD "known-secret-password-1"
#define NEW_PASSWORD "known-secret-password-2"
#define RESET_PASSWORD "known-secret-password-3"
#define TYPED_INTO_NAME "known-secret-typed-as-name"

static const uint8_t known_key[] = {0xDE, 0xAD, 0xBE, 0xEF, 0xFE, 0xED, 0xFA, 0xCE};
static const uint8_t other_key[] = {0xCA, 0xFE, 0xBA, 0xBE, 0x01, 0x02, 0x03, 0x04};

static bool contains(const void *haystack, size_t haystack_size, const void *needle,
                     size_t needle_size)
{
    const uint8_t *bytes = haystack;
    if (needle_size == 0 || haystack_size < needle_size) {
        return false;
    }
    for (size_t offset = 0; offset <= haystack_size - needle_size; ++offset) {
        if (memcmp(&bytes[offset], needle, needle_size) == 0) {
            return true;
        }
    }
    return false;
}

/* The port contract: events run with no lock held. A held nonrecursive lock refuses this poll. */
static void hook_expect_no_lock_held(void *context)
{
    (void)context;
    const crux_mutex_t locks[] = {crux_access_state.state_lock, crux_access_state.change_lock};
    for (size_t index = 0; index < 2; ++index) {
        crux_status_t status = crux_mutex_lock(locks[index], 0);
        CHECK(status == CRUX_OK);
        if (status == CRUX_OK) {
            (void)crux_mutex_unlock(locks[index]);
        }
    }
}

static void exercise_every_path(void)
{
    const crux_access_policy_t *policy = &access_test_profile_reference.policy;
    access_test_boot(policy);
    access_fake_event_hook(hook_expect_no_lock_held, NULL);
    crux_access_session_t session = {0};
    uint32_t retry_after_ms = 0;
    const size_t length = strlen(PASSWORD);
    const size_t new_length = strlen(NEW_PASSWORD);
    const size_t reset_length = strlen(RESET_PASSWORD);

    CHECK(crux_access_provision_first_account("admin", PASSWORD, length, "administrator",
                                              &access_test_serial) == CRUX_ACCESS_OK);
    crux_access_session_t admin = access_test_session("admin", PASSWORD, &access_test_web);

    /* Every failure kind, including a password typed into the name field. */
    (void)access_test_login("admin", NEW_PASSWORD, &access_test_web, &session);
    (void)access_test_login("no-such-user", PASSWORD, &access_test_web, &session);
    (void)access_test_login(TYPED_INTO_NAME, PASSWORD, &access_test_web, &session);

    /* Account changes that carry secrets. */
    CHECK(crux_access_account_create(admin, "alice", "operator", PASSWORD, length) ==
          CRUX_ACCESS_OK);
    CHECK(crux_access_account_add_key(admin, "alice", known_key, sizeof(known_key)) ==
          CRUX_ACCESS_OK);
    crux_access_session_t alice = access_test_session("alice", PASSWORD, &access_test_web_other);
    CHECK(crux_access_check(alice, CRUX_ACCESS_PERMISSION_MANAGE) == CRUX_ACCESS_ERR_DENIED);
    CHECK(crux_access_account_remove(alice, "admin") == CRUX_ACCESS_ERR_DENIED);
    CHECK(crux_access_account_change_password(alice, PASSWORD, length, NEW_PASSWORD, new_length,
                                              &retry_after_ms) == CRUX_ACCESS_OK);
    (void)crux_access_account_change_password(alice, RESET_PASSWORD, reset_length, NEW_PASSWORD,
                                              new_length, &retry_after_ms);
    CHECK(crux_access_account_reset_password(admin, "alice", RESET_PASSWORD, reset_length) ==
          CRUX_ACCESS_OK);
    crux_access_session_t forced = access_test_session("alice", RESET_PASSWORD, &access_test_web);
    CHECK(crux_access_account_change_password(forced, RESET_PASSWORD, reset_length, NEW_PASSWORD,
                                              new_length, &retry_after_ms) == CRUX_ACCESS_OK);
    crux_access_logout(&forced);

    /* Keys: signed and unsigned, listed and not, and a failed signature. */
    CHECK(crux_access_key_is_authorised("alice", known_key, sizeof(known_key), false,
                                        &access_test_ssh, &retry_after_ms) == CRUX_ACCESS_OK);
    CHECK(crux_access_key_is_authorised("alice", other_key, sizeof(other_key), true,
                                        &access_test_ssh,
                                        &retry_after_ms) == CRUX_ACCESS_ERR_DENIED);
    crux_access_report_key_failure("alice", &access_test_ssh);
    crux_access_report_key_failure(TYPED_INTO_NAME, &access_test_ssh);
    CHECK(crux_access_login_verified("alice", known_key, sizeof(known_key), &access_test_ssh,
                                     &session) == CRUX_ACCESS_OK);
    CHECK(crux_access_login_verified("alice", other_key, sizeof(other_key), &access_test_ssh,
                                     &session) == CRUX_ACCESS_ERR_DENIED);
    CHECK(crux_access_account_remove_key(admin, "alice", known_key, sizeof(known_key)) ==
          CRUX_ACCESS_OK);

    /* The rest of the account changes, an expiry and a delay. */
    CHECK(crux_access_account_set_role(admin, "alice", "viewer") == CRUX_ACCESS_OK);
    CHECK(crux_access_account_disable(admin, "alice") == CRUX_ACCESS_OK);
    CHECK(crux_access_account_enable(admin, "alice") == CRUX_ACCESS_OK);
    CHECK(crux_access_account_remove(admin, "alice") == CRUX_ACCESS_OK);
    crux_access_origin_t origin = access_test_web_from(1);
    for (unsigned attempt = 0; attempt < CRUX_ACCESS_DELAY_FREE_ATTEMPTS + 2u; ++attempt) {
        (void)access_test_login(TYPED_INTO_NAME, PASSWORD, &origin, &session);
    }
    access_fake_clock_advance((uint64_t)policy->session_idle_limit_s * 1000u);
    CHECK(crux_access_account_remove(admin, "admin") == CRUX_ACCESS_ERR_SESSION_INVALID);
    access_fake_event_hook(NULL, NULL);
}

static void every_event_type_was_reached(void)
{
    const crux_access_event_type_t types[] = {
        CRUX_ACCESS_EVENT_LOGIN_SUCCESS,
        CRUX_ACCESS_EVENT_LOGIN_FAILURE,
        CRUX_ACCESS_EVENT_KEY_FAILURE,
        CRUX_ACCESS_EVENT_DELAY_APPLIED,
        CRUX_ACCESS_EVENT_PERMISSION_DENIED,
        CRUX_ACCESS_EVENT_LOGOUT,
        CRUX_ACCESS_EVENT_SESSION_EXPIRED,
        CRUX_ACCESS_EVENT_PROVISIONED,
        CRUX_ACCESS_EVENT_ACCOUNT_CREATED,
        CRUX_ACCESS_EVENT_ACCOUNT_REMOVED,
        CRUX_ACCESS_EVENT_ACCOUNT_DISABLED,
        CRUX_ACCESS_EVENT_ACCOUNT_ENABLED,
        CRUX_ACCESS_EVENT_ACCOUNT_ROLE_CHANGED,
        CRUX_ACCESS_EVENT_ACCOUNT_PASSWORD_RESET,
        CRUX_ACCESS_EVENT_ACCOUNT_PASSWORD_CHANGED,
        CRUX_ACCESS_EVENT_ACCOUNT_KEY_ADDED,
        CRUX_ACCESS_EVENT_ACCOUNT_KEY_REMOVED,
    };
    for (size_t index = 0; index < sizeof(types) / sizeof(types[0]); ++index) {
        CHECK(access_fake_event_count_of(types[index]) > 0);
    }
}

static void no_event_holds_a_secret(void)
{
    const char *const secrets[] = {PASSWORD, NEW_PASSWORD, RESET_PASSWORD, TYPED_INTO_NAME};
    /* Each secret, and enough of its start to catch a truncated copy. */
    for (size_t event = 0; event < access_fake_event_count(); ++event) {
        const crux_access_event_t *recorded = access_fake_event(event);
        for (size_t index = 0; index < sizeof(secrets) / sizeof(secrets[0]); ++index) {
            CHECK(!contains(recorded, sizeof(*recorded), secrets[index], 12));
            CHECK(strstr(recorded->account, "known-secret") == NULL);
        }
        CHECK(!contains(recorded, sizeof(*recorded), known_key, 4));
        CHECK(!contains(recorded, sizeof(*recorded), other_key, 4));
    }
}

/* What the medium holds is declared under Issue #178, but it must never hold a plain password. */
static void no_plain_password_on_the_medium(void)
{
    const char *const secrets[] = {PASSWORD, NEW_PASSWORD, RESET_PASSWORD};
    size_t length = 0;
    const uint8_t *stored = access_fake_store_record(&length);
    CHECK(length > 0);
    for (size_t index = 0; index < sizeof(secrets) / sizeof(secrets[0]); ++index) {
        CHECK(!contains(stored, length, secrets[index], strlen(secrets[index])));
    }
}

/* A change must not leave a derived value behind in the pending table or the save buffer. */
static size_t copies_in_state(const uint8_t *derived)
{
    size_t copies = 0;
    const uint8_t *state = (const uint8_t *)&crux_access_state;
    for (size_t offset = 0; offset + CRUX_ACCESS_DERIVED_SIZE <= sizeof(crux_access_state);
         ++offset) {
        copies += memcmp(&state[offset], derived, CRUX_ACCESS_DERIVED_SIZE) == 0 ? 1u : 0u;
    }
    return copies;
}

/* A removed account's verifier and keys must not stay behind the end of the live table. */
static void no_verifier_left_after_removal(void)
{
    const crux_access_policy_t *policy = &access_test_profile_reference.policy;
    access_test_boot(policy);
    CHECK(crux_access_provision_first_account("admin", PASSWORD, strlen(PASSWORD), "administrator",
                                              &access_test_serial) == CRUX_ACCESS_OK);
    crux_access_session_t admin = access_test_session("admin", PASSWORD, &access_test_web);
    CHECK(crux_access_account_create(admin, "alice", "viewer", PASSWORD, strlen(PASSWORD)) ==
          CRUX_ACCESS_OK);
    CHECK(crux_access_account_add_key(admin, "alice", known_key, sizeof(known_key)) ==
          CRUX_ACCESS_OK);
    size_t length = 0;
    access_table_t table;
    CHECK(crux_access_record_decode(access_fake_store_record(&length), length, &table));
    uint8_t derived[CRUX_ACCESS_DERIVED_SIZE];
    memcpy(derived, table.accounts[1].derived, sizeof(derived));
    CHECK(copies_in_state(derived) == 1);
    CHECK(crux_access_account_remove(admin, "alice") == CRUX_ACCESS_OK);
    CHECK(copies_in_state(derived) == 0);
    CHECK(!contains(&crux_access_state, sizeof(crux_access_state), known_key, sizeof(known_key)));
    /* The same when the removed account is not the last in the table. */
    CHECK(crux_access_account_create(admin, "alice", "viewer", PASSWORD, strlen(PASSWORD)) ==
          CRUX_ACCESS_OK);
    CHECK(crux_access_account_create(admin, "bob", "viewer", PASSWORD, strlen(PASSWORD)) ==
          CRUX_ACCESS_OK);
    CHECK(crux_access_record_decode(access_fake_store_record(&length), length, &table));
    memcpy(derived, table.accounts[1].derived, sizeof(derived));
    uint8_t moved[CRUX_ACCESS_DERIVED_SIZE];
    memcpy(moved, table.accounts[2].derived, sizeof(moved));
    CHECK(crux_access_account_remove(admin, "alice") == CRUX_ACCESS_OK);
    CHECK(copies_in_state(derived) == 0);
    CHECK(copies_in_state(moved) == 1);
}

static void no_verifier_left_in_scratch_state(void)
{
    size_t length = 0;
    const uint8_t *stored = access_fake_store_record(&length);
    access_table_t table;
    CHECK(crux_access_record_decode(stored, length, &table));
    const uint8_t *derived = table.accounts[0].derived;
    /* Exactly the live table's own copy. */
    CHECK(copies_in_state(derived) == 1);
}

int main(void)
{
    exercise_every_path();
    every_event_type_was_reached();
    no_event_holds_a_secret();
    no_plain_password_on_the_medium();
    no_verifier_left_in_scratch_state();
    no_verifier_left_after_removal();
    return access_test_finish("Access secret absence");
}
