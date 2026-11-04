/* Signed and unsigned key attempts, failed signatures and provider-verified login. */
#include "access_test.h"

#define ADMIN ACCESS_TEST_ADMIN
#define USER ACCESS_TEST_USER
#define PASSWORD ACCESS_TEST_PASSWORD
#define WRONG "wrong-password-here"

static const uint8_t listed[] = ACCESS_TEST_ED25519_KEY(0xA1);
static const uint8_t unlisted[] = ACCESS_TEST_ED25519_KEY(0xB1);

static crux_access_session_t setup(void)
{
    access_test_boot_with_admin();
    crux_access_session_t admin = access_test_session(ADMIN, PASSWORD, &access_test_serial);
    access_test_create_user(admin);
    CHECK(crux_access_account_add_key(admin, USER, listed, sizeof(listed)) == CRUX_ACCESS_OK);
    access_fake_events_clear();
    access_fake_derive_counters_clear();
    return admin;
}

static crux_access_result_t probe(const char *user, const uint8_t *key, size_t key_length,
                                  bool signed_request, const crux_access_origin_t *origin)
{
    uint32_t retry_after_ms = 0;
    return crux_access_key_is_authorised(user, key, key_length, signed_request, origin,
                                         &retry_after_ms);
}

static void unsigned_probe_cases(void)
{
    (void)setup();
    /* A client normally offers keys this way, so a probe records nothing. */
    for (unsigned attempt = 0; attempt < 50; ++attempt) {
        CHECK(probe(USER, unlisted, sizeof(unlisted), false, &access_test_ssh) ==
              CRUX_ACCESS_ERR_DENIED);
        CHECK(probe("no-such-user", listed, sizeof(listed), false, &access_test_ssh) ==
              CRUX_ACCESS_ERR_DENIED);
    }
    CHECK(probe(USER, listed, sizeof(listed), false, &access_test_ssh) == CRUX_ACCESS_OK);
    CHECK(access_fake_event_count() == 0);
    CHECK(access_fake_derive_count() == 0);

    /* It never creates a session, and a listed key with no signature proves nothing. */
    CHECK(crux_access_login_verified(USER, unlisted, sizeof(unlisted), &access_test_ssh,
                                     &(crux_access_session_t){0}) == CRUX_ACCESS_ERR_DENIED);
}

static void signed_request_cases(void)
{
    (void)setup();
    uint32_t retry_after_ms = 7;

    /* A listed key passes without counting. */
    CHECK(crux_access_key_is_authorised(USER, listed, sizeof(listed), true, &access_test_ssh,
                                        &retry_after_ms) == CRUX_ACCESS_OK);
    CHECK(retry_after_ms == 0);
    CHECK(access_fake_event_count() == 0);

    /* An unlisted key on a signed request is a key failure, with an event. */
    for (unsigned attempt = 0; attempt < CRUX_ACCESS_DELAY_FREE_ATTEMPTS + 1u; ++attempt) {
        CHECK(probe(USER, unlisted, sizeof(unlisted), true, &access_test_ssh) ==
              CRUX_ACCESS_ERR_DENIED);
    }
    CHECK(access_fake_event_count_of(CRUX_ACCESS_EVENT_KEY_FAILURE) ==
          CRUX_ACCESS_DELAY_FREE_ATTEMPTS + 1u);
    CHECK(strcmp(access_fake_event(0)->account, USER) == 0);
    CHECK(access_fake_event(0)->result == CRUX_ACCESS_ERR_DENIED);
    CHECK(access_fake_event(0)->origin.channel == CRUX_ACCESS_CHANNEL_SSH);

    /* Inside a window: retry later, without counting or extending, for signed and unsigned alike.
     */
    access_fake_events_clear();
    CHECK(crux_access_key_is_authorised(USER, unlisted, sizeof(unlisted), true, &access_test_ssh,
                                        &retry_after_ms) == CRUX_ACCESS_ERR_RETRY_LATER);
    CHECK(retry_after_ms == CRUX_ACCESS_DELAY_BASE_MS);
    CHECK(access_fake_event_count_of(CRUX_ACCESS_EVENT_KEY_FAILURE) == 0);
    CHECK(access_fake_event_count_of(CRUX_ACCESS_EVENT_DELAY_APPLIED) == 1);
    CHECK(crux_access_key_is_authorised(USER, listed, sizeof(listed), false, &access_test_ssh,
                                        &retry_after_ms) == CRUX_ACCESS_ERR_RETRY_LATER);
    /* An unsigned probe records nothing, even inside a window. */
    CHECK(access_fake_event_count_of(CRUX_ACCESS_EVENT_DELAY_APPLIED) == 1);
    access_fake_clock_advance(CRUX_ACCESS_DELAY_BASE_MS - 1u);
    CHECK(crux_access_key_is_authorised(USER, listed, sizeof(listed), true, &access_test_ssh,
                                        &retry_after_ms) == CRUX_ACCESS_ERR_RETRY_LATER);
    CHECK(retry_after_ms == 1);
    access_fake_clock_advance(1);
    CHECK(probe(USER, listed, sizeof(listed), true, &access_test_ssh) == CRUX_ACCESS_OK);

    /* An unknown name omits the supplied name, which may be a mistyped password. */
    (void)setup();
    CHECK(probe("my-password-typed-here", unlisted, sizeof(unlisted), true, &access_test_ssh) ==
          CRUX_ACCESS_ERR_DENIED);
    CHECK(access_fake_event(0)->account[0] == '\0');

    /* A key longer than any listed key is simply not listed, and counts when signed. */
    uint8_t oversized[CRUX_ACCESS_KEY_SIZE_MAX + 1u];
    memset(oversized, 0xCC, sizeof(oversized));
    access_fake_events_clear();
    CHECK(probe(USER, oversized, sizeof(oversized), true, &access_test_web_other) ==
          CRUX_ACCESS_ERR_DENIED);
    CHECK(access_fake_event_count_of(CRUX_ACCESS_EVENT_KEY_FAILURE) == 1);

    /* Invalid input. */
    CHECK(probe(NULL, listed, sizeof(listed), false, &access_test_ssh) ==
          CRUX_ACCESS_ERR_INVALID_ARGUMENT);
    CHECK(probe(USER, NULL, sizeof(listed), false, &access_test_ssh) ==
          CRUX_ACCESS_ERR_INVALID_ARGUMENT);
    CHECK(probe(USER, listed, 0, false, &access_test_ssh) == CRUX_ACCESS_ERR_INVALID_ARGUMENT);
    CHECK(probe(USER, listed, sizeof(listed), false, NULL) == CRUX_ACCESS_ERR_INVALID_ARGUMENT);
    CHECK(crux_access_key_is_authorised(USER, listed, sizeof(listed), false, &access_test_ssh,
                                        NULL) == CRUX_ACCESS_ERR_INVALID_ARGUMENT);
}

static void failed_signature_cases(void)
{
    (void)setup();
    /* A failed signature counts towards the source delay, with an event. */
    for (unsigned attempt = 0; attempt < CRUX_ACCESS_DELAY_FREE_ATTEMPTS + 1u; ++attempt) {
        crux_access_report_key_failure(USER, &access_test_ssh);
    }
    CHECK(access_fake_event_count_of(CRUX_ACCESS_EVENT_KEY_FAILURE) ==
          CRUX_ACCESS_DELAY_FREE_ATTEMPTS + 1u);
    CHECK(strcmp(access_fake_event(0)->account, USER) == 0);
    uint32_t retry_after_ms = 0;
    CHECK(crux_access_key_is_authorised(USER, listed, sizeof(listed), false, &access_test_ssh,
                                        &retry_after_ms) == CRUX_ACCESS_ERR_RETRY_LATER);
    CHECK(retry_after_ms == CRUX_ACCESS_DELAY_BASE_MS);

    /* A report inside a window neither counts nor extends it, but still leaves an event. */
    access_fake_clock_advance(500);
    size_t events = access_fake_event_count_of(CRUX_ACCESS_EVENT_KEY_FAILURE);
    crux_access_report_key_failure(USER, &access_test_ssh);
    CHECK(access_fake_event_count_of(CRUX_ACCESS_EVENT_KEY_FAILURE) == events + 1u);
    CHECK(crux_access_key_is_authorised(USER, listed, sizeof(listed), false, &access_test_ssh,
                                        &retry_after_ms) == CRUX_ACCESS_ERR_RETRY_LATER);
    CHECK(retry_after_ms == 500);

    /* The name delay is untouched, so the account's password login from elsewhere still works. */
    crux_access_origin_t origin = access_test_web_from(1);
    (void)access_test_session(USER, PASSWORD, &origin);

    /* Unknown, malformed and missing names omit the name and still count. A NULL origin is ignored.
     */
    access_fake_events_clear();
    crux_access_report_key_failure("ghost", &access_test_web_other);
    CHECK(access_fake_event(0)->account[0] == '\0');
    crux_access_report_key_failure("Not Valid", &access_test_web_other);
    crux_access_report_key_failure(NULL, &access_test_web_other);
    CHECK(access_fake_event_count() == 3);
    crux_access_report_key_failure(USER, NULL);
    CHECK(access_fake_event_count() == 3);
}

static void login_verified_cases(void)
{
    crux_access_session_t admin = setup();
    crux_access_session_t session = {0};

    CHECK(crux_access_login_verified(USER, listed, sizeof(listed), &access_test_ssh, &session) ==
          CRUX_ACCESS_OK);
    CHECK(session.value != 0);
    CHECK(crux_access_check(session, "config.write") == CRUX_ACCESS_OK);
    CHECK(access_fake_derive_count() == 0);
    CHECK(access_fake_event_count_of(CRUX_ACCESS_EVENT_LOGIN_SUCCESS) == 1);

    /* It leaves delay state alone: refusals do not count, and the name delay does not apply. */
    crux_access_session_t refused = {0x1234u};
    access_fake_events_clear();
    for (unsigned attempt = 0; attempt < 20; ++attempt) {
        CHECK(crux_access_login_verified(USER, unlisted, sizeof(unlisted), &access_test_ssh,
                                         &refused) == CRUX_ACCESS_ERR_DENIED);
        CHECK(refused.value == 0);
    }
    CHECK(access_fake_event_count_of(CRUX_ACCESS_EVENT_LOGIN_REFUSED) == 20);
    CHECK(strcmp(access_fake_event(0)->account, USER) == 0);
    CHECK(probe(USER, listed, sizeof(listed), false, &access_test_ssh) == CRUX_ACCESS_OK);

    /* A password flood on the name does not block key login. */
    uint32_t source = 1;
    for (unsigned attempt = 0; attempt < 20; ++attempt) {
        crux_access_origin_t origin = access_test_web_from(source);
        source += 1;
        (void)access_test_login(USER, WRONG, &origin, &refused);
    }
    crux_access_origin_t origin = access_test_web_from(source);
    CHECK(access_test_login(USER, PASSWORD, &origin, &refused) == CRUX_ACCESS_ERR_RETRY_LATER);
    CHECK(crux_access_login_verified(USER, listed, sizeof(listed), &access_test_ssh, &session) ==
          CRUX_ACCESS_OK);

    /* Refused for an account that changed since the pre-check, and for unknown names. */
    CHECK(crux_access_account_disable(admin, USER) == CRUX_ACCESS_OK);
    CHECK(crux_access_login_verified(USER, listed, sizeof(listed), &access_test_ssh, &session) ==
          CRUX_ACCESS_ERR_DENIED);
    CHECK(session.value == 0);
    access_fake_events_clear();
    CHECK(crux_access_login_verified("my-password-typed-here", listed, sizeof(listed),
                                     &access_test_ssh, &session) == CRUX_ACCESS_ERR_DENIED);
    CHECK(access_fake_event(0)->account[0] == '\0');
    /* A disabled account lists no keys for the pre-check. */
    CHECK(probe(USER, listed, sizeof(listed), false, &access_test_ssh) == CRUX_ACCESS_ERR_DENIED);
    CHECK(crux_access_account_enable(admin, USER) == CRUX_ACCESS_OK);

    /* A key removed since the pre-check is refused. */
    CHECK(probe(USER, listed, sizeof(listed), false, &access_test_ssh) == CRUX_ACCESS_OK);
    CHECK(crux_access_account_remove_key(admin, USER, listed, sizeof(listed)) == CRUX_ACCESS_OK);
    CHECK(crux_access_login_verified(USER, listed, sizeof(listed), &access_test_ssh, &session) ==
          CRUX_ACCESS_ERR_DENIED);

    /* Invalid input. */
    CHECK(crux_access_login_verified(NULL, listed, sizeof(listed), &access_test_ssh, &session) ==
          CRUX_ACCESS_ERR_INVALID_ARGUMENT);
    CHECK(crux_access_login_verified(USER, NULL, sizeof(listed), &access_test_ssh, &session) ==
          CRUX_ACCESS_ERR_INVALID_ARGUMENT);
    CHECK(crux_access_login_verified(USER, listed, 0, &access_test_ssh, &session) ==
          CRUX_ACCESS_ERR_INVALID_ARGUMENT);
    CHECK(crux_access_login_verified(USER, listed, sizeof(listed), NULL, &session) ==
          CRUX_ACCESS_ERR_INVALID_ARGUMENT);
    CHECK(crux_access_login_verified(USER, listed, sizeof(listed), &access_test_ssh, NULL) ==
          CRUX_ACCESS_ERR_INVALID_ARGUMENT);

    /* A role that left the policy refuses key login too, and the event says so. */
    crux_access_session_t again = setup();
    (void)again;
    static const char *const manage[] = {CRUX_ACCESS_PERMISSION_MANAGE};
    static const crux_access_role_t reduced_roles[] = {{"administrator", manage, 1}};
    static const crux_access_policy_t reduced = {reduced_roles, 1, 900, 28800, NULL};
    access_test_power_cycle(&reduced);
    access_fake_events_clear();
    CHECK(crux_access_login_verified(USER, listed, sizeof(listed), &access_test_ssh, &session) ==
          CRUX_ACCESS_ERR_DENIED);
    CHECK(access_fake_event(0)->role_missing);
}

/* Appends one length-prefixed SSH string. */
static size_t blob_string(uint8_t *blob, const void *text, size_t length)
{
    blob[0] = 0;
    blob[1] = 0;
    blob[2] = (uint8_t)(length >> 8);
    blob[3] = (uint8_t)length;
    if (length > 0) {
        memcpy(&blob[4], text, length);
    }
    return 4u + length;
}

/* An ECDSA blob whose point is all one value, after a first byte that says how it is encoded. */
static size_t ecdsa_blob(uint8_t *blob, const char *type, size_t point_size, const char *curve,
                         uint8_t encoding)
{
    uint8_t point[256];
    memset(point, 0x42, sizeof(point));
    point[0] = encoding;
    size_t used = blob_string(blob, type, strlen(type));
    used += blob_string(&blob[used], curve, strlen(curve));
    used += blob_string(&blob[used], point, point_size);
    return used;
}

/* Only Ed25519 and ECDSA keys are accepted, in the SSH blob form. */
static void key_type_cases(void)
{
    crux_access_session_t admin = setup();
    uint8_t blob[512];
    size_t used = 0;
    const char *const other_user = "bob";
    CHECK(crux_access_account_create(admin, other_user, "viewer", PASSWORD, strlen(PASSWORD)) ==
          CRUX_ACCESS_OK);

    used = ecdsa_blob(blob, "ecdsa-sha2-nistp256", 65, "nistp256", 4);
    CHECK(crux_access_account_add_key(admin, other_user, blob, used) == CRUX_ACCESS_OK);
    CHECK(crux_access_account_remove_key(admin, other_user, blob, used) == CRUX_ACCESS_OK);
    used = ecdsa_blob(blob, "ecdsa-sha2-nistp384", 97, "nistp384", 4);
    CHECK(crux_access_account_add_key(admin, other_user, blob, used) == CRUX_ACCESS_OK);
    CHECK(crux_access_account_remove_key(admin, other_user, blob, used) == CRUX_ACCESS_OK);
    used = ecdsa_blob(blob, "ecdsa-sha2-nistp521", 133, "nistp521", 4);
    CHECK(used <= CRUX_ACCESS_KEY_SIZE_MAX);
    CHECK(crux_access_account_add_key(admin, other_user, blob, used) == CRUX_ACCESS_OK);
    CHECK(crux_access_account_remove_key(admin, other_user, blob, used) == CRUX_ACCESS_OK);

    /* A small RSA key fits the size limit, and its type still refuses it. */
    used = blob_string(blob, "ssh-rsa", 7);
    uint8_t exponent[] = {1, 0, 1};
    uint8_t modulus[128];
    memset(modulus, 0xC3, sizeof(modulus));
    used += blob_string(&blob[used], exponent, sizeof(exponent));
    used += blob_string(&blob[used], modulus, sizeof(modulus));
    CHECK(used <= CRUX_ACCESS_KEY_SIZE_MAX);
    CHECK(crux_access_account_add_key(admin, other_user, blob, used) ==
          CRUX_ACCESS_ERR_INVALID_ARGUMENT);

    /* Every malformed shape. */
    const uint8_t ed25519[] = ACCESS_TEST_ED25519_KEY(7);
    const uint8_t opaque[] = {1, 2, 3};
    CHECK(crux_access_account_add_key(admin, other_user, opaque, sizeof(opaque)) ==
          CRUX_ACCESS_ERR_INVALID_ARGUMENT);
    for (size_t cut = 1; cut < sizeof(ed25519); ++cut) {
        CHECK(crux_access_account_add_key(admin, other_user, ed25519, cut) ==
              CRUX_ACCESS_ERR_INVALID_ARGUMENT);
    }
    memcpy(blob, ed25519, sizeof(ed25519));
    blob[sizeof(ed25519)] = 0;
    CHECK(crux_access_account_add_key(admin, other_user, blob, sizeof(ed25519) + 1u) ==
          CRUX_ACCESS_ERR_INVALID_ARGUMENT);
    /* A type name that only starts like an accepted one. */
    memcpy(blob, ed25519, sizeof(ed25519));
    blob[3] = 10;
    CHECK(crux_access_account_add_key(admin, other_user, blob, sizeof(ed25519)) ==
          CRUX_ACCESS_ERR_INVALID_ARGUMENT);
    /* A length that claims more than the blob holds. */
    memcpy(blob, ed25519, sizeof(ed25519));
    blob[0] = 0xFF;
    CHECK(crux_access_account_add_key(admin, other_user, blob, sizeof(ed25519)) ==
          CRUX_ACCESS_ERR_INVALID_ARGUMENT);
    /* Curves that disagree, a point of the wrong size and a compressed point. */
    used = ecdsa_blob(blob, "ecdsa-sha2-nistp256", 65, "nistp384", 4);
    CHECK(crux_access_account_add_key(admin, other_user, blob, used) ==
          CRUX_ACCESS_ERR_INVALID_ARGUMENT);
    used = ecdsa_blob(blob, "ecdsa-sha2-nistp256", 64, "nistp256", 4);
    CHECK(crux_access_account_add_key(admin, other_user, blob, used) ==
          CRUX_ACCESS_ERR_INVALID_ARGUMENT);
    used = ecdsa_blob(blob, "ecdsa-sha2-nistp256", 65, "nistp256", 2);
    CHECK(crux_access_account_add_key(admin, other_user, blob, used) ==
          CRUX_ACCESS_ERR_INVALID_ARGUMENT);
    crux_access_account_info_t info[CRUX_ACCESS_ACCOUNT_MAX];
    size_t count = 0;
    CHECK(crux_access_account_list(admin, info, CRUX_ACCESS_ACCOUNT_MAX, &count) == CRUX_ACCESS_OK);
    CHECK(count == 3 && info[2].key_count == 0);

    /* A refused type is simply not listed, so a signed attempt with one is a key failure. */
    access_fake_events_clear();
    used = blob_string(blob, "ssh-rsa", 7);
    used += blob_string(&blob[used], exponent, sizeof(exponent));
    used += blob_string(&blob[used], modulus, sizeof(modulus));
    CHECK(probe(USER, blob, used, true, &access_test_ssh) == CRUX_ACCESS_ERR_DENIED);
    CHECK(access_fake_event_count_of(CRUX_ACCESS_EVENT_KEY_FAILURE) == 1);
}

int main(void)
{
    key_type_cases();
    unsigned_probe_cases();
    signed_request_cases();
    failed_signature_cases();
    login_verified_cases();
    return access_test_finish("Access keys");
}
