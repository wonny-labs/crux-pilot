/*
 * The real crux_access core on the real store, through the adapter a composition would link. Access
 * has its own suites for itself: these cases are what the two contracts promise each other, AC6 and
 * AC8 and decisions S6 and S7. Derivation, hashing and the clock are test ports here.
 */
#include "access_port.h"
#include "credential_test.h"
#include "fake_file.h"

static const char *const administrator_permissions[] = {CRUX_ACCESS_PERMISSION_MANAGE};
static const crux_access_role_t roles[] = {{"administrator", administrator_permissions, 1}};
static const crux_access_policy_t policy = {roles, 1, 900u, 28800u, NULL};
static const crux_access_origin_t serial = {CRUX_ACCESS_CHANNEL_SERIAL, {0}, 0};

#define ADMIN "admin"
#define OTHER "alice"
#define PASSWORD "correct horse battery"

/* Available only in the instrumented Access core. */
void crux_access_test_reset(void);

/* The boot order a composition must use: the store, then Access. */
static crux_access_result_t boot_both(void)
{
    (void)test_boot();
    crux_access_test_reset();
    return crux_access_init(&policy);
}

static crux_access_result_t provision(void)
{
    return crux_access_provision_first_account(ADMIN, PASSWORD, strlen(PASSWORD), "administrator",
                                               &serial);
}

static crux_access_result_t login(crux_access_session_t *session)
{
    uint32_t retry_after_ms = 0;
    return crux_access_login_password(ADMIN, PASSWORD, strlen(PASSWORD), &serial, session,
                                      &retry_after_ms);
}

static bool provisioning_open(void)
{
    /* The password rule is reached only while provisioning is open. */
    return crux_access_provision_first_account(ADMIN, "short", 5u, "administrator", &serial) ==
           CRUX_ACCESS_ERR_PASSWORD_REJECTED;
}

/* AC6: EMPTY in the access port means no account record, and device keys can exist first. */
static void test_keys_do_not_close_provisioning(void)
{
    test_fresh();
    CHECK(test_boot() == CRUX_CREDENTIAL_OK);
    CHECK(crux_credential_key_ensure(CRUX_CREDENTIAL_RECORD_SSH_HOST_KEY,
                                     CRUX_CREDENTIAL_KEY_ED25519) == CRUX_CREDENTIAL_OK);
    CHECK(crux_credential_key_ensure(CRUX_CREDENTIAL_RECORD_TLS_KEY,
                                     CRUX_CREDENTIAL_KEY_ECDSA_P256) == CRUX_CREDENTIAL_OK);
    CHECK(crux_credential_state() == CRUX_CREDENTIAL_STATE_VALID);

    CHECK(boot_both() == CRUX_ACCESS_OK);
    CHECK(crux_credential_state() == CRUX_CREDENTIAL_STATE_VALID);
    CHECK(provision() == CRUX_ACCESS_OK);

    /* Accounts stay next to the keys, across a reset. */
    CHECK(boot_both() == CRUX_ACCESS_OK);
    crux_access_session_t session = {0};
    CHECK(login(&session) == CRUX_ACCESS_OK);
    CHECK(crux_access_check(session, CRUX_ACCESS_PERMISSION_MANAGE) == CRUX_ACCESS_OK);
    CHECK(provision() == CRUX_ACCESS_ERR_DENIED);
    uint8_t public_key[CRUX_CREDENTIAL_KEY_PUBLIC_BYTES_MAX];
    size_t length = 0;
    CHECK(crux_credential_key_public(CRUX_CREDENTIAL_RECORD_SSH_HOST_KEY, public_key,
                                     sizeof(public_key), &length) == CRUX_CREDENTIAL_OK);
}

/* AC6: a blank card that fails at mount leaves the roots unbound, which is UNUSABLE, not EMPTY. */
static void test_store_that_did_not_mount(void)
{
    test_fresh();
    CHECK(crux_credential_start(TEST_ROOT_UNBOUND, TEST_ROOT_UNBOUND - 1) ==
          CRUX_CREDENTIAL_ERR_UNUSABLE);
    crux_access_test_reset();
    CHECK(crux_access_init(&policy) == CRUX_ACCESS_ERR_NOT_READY);
    CHECK(provision() == CRUX_ACCESS_ERR_DENIED);

    /* A medium inserted while running does not reopen provisioning. */
    CHECK(crux_access_init(&policy) == CRUX_ACCESS_ERR_NOT_READY);
    CHECK(provision() == CRUX_ACCESS_ERR_DENIED);
}

/* Access started before the store, which is a composition mistake, fails closed too. */
static void test_store_not_started(void)
{
    test_fresh();
    crux_access_test_reset();
    CHECK(crux_access_init(&policy) == CRUX_ACCESS_ERR_NOT_READY);
    CHECK(provision() == CRUX_ACCESS_ERR_DENIED);
}

/* A6: anything but a proven absence keeps provisioning closed. */
static void test_corrupt_stores_stay_closed(void)
{
    /* Both slots corrupt. */
    test_fresh();
    CHECK(boot_both() == CRUX_ACCESS_OK);
    CHECK(provision() == CRUX_ACCESS_OK);
    test_slot_flip(TEST_SLOT_A, CRUX_CREDENTIAL_HEADER_BYTES, 0x01);
    test_slot_flip(TEST_SLOT_B, CRUX_CREDENTIAL_HEADER_BYTES, 0x01);
    CHECK(boot_both() == CRUX_ACCESS_ERR_NOT_READY);
    CHECK(provision() == CRUX_ACCESS_ERR_DENIED);

    /* One slot corrupt: the other serves, with the account the first save put in both. */
    test_fresh();
    CHECK(boot_both() == CRUX_ACCESS_OK);
    CHECK(provision() == CRUX_ACCESS_OK);
    test_slot_flip(TEST_SLOT_A, CRUX_CREDENTIAL_HEADER_BYTES, 0x01);
    CHECK(boot_both() == CRUX_ACCESS_OK);
    crux_access_session_t session = {0};
    CHECK(login(&session) == CRUX_ACCESS_OK);
    CHECK(provision() == CRUX_ACCESS_ERR_DENIED);

    /* A record the store opened but Access cannot decode is unusable to Access. */
    test_fresh();
    CHECK(test_boot() == CRUX_CREDENTIAL_OK);
    const uint8_t garbage[] = "not an account table";
    CHECK(crux_credential_write(CRUX_CREDENTIAL_RECORD_ACCOUNTS, garbage, sizeof(garbage)) ==
          CRUX_CREDENTIAL_OK);
    CHECK(boot_both() == CRUX_ACCESS_ERR_NOT_READY);
    CHECK(provision() == CRUX_ACCESS_ERR_DENIED);

    /* A read error at start. */
    test_fresh();
    CHECK(boot_both() == CRUX_ACCESS_OK);
    CHECK(provision() == CRUX_ACCESS_OK);
    crux_file_fake_fail(CRUX_FILE_FAKE_READ, 0, CRUX_FILE_ERR_IO);
    CHECK(boot_both() == CRUX_ACCESS_ERR_NOT_READY);
    CHECK(provision() == CRUX_ACCESS_ERR_DENIED);

    /* A read error between the store's start and Access's: the store is VALID, the read fails, and
     * that must not read as absent. */
    test_fresh();
    CHECK(boot_both() == CRUX_ACCESS_OK);
    CHECK(provision() == CRUX_ACCESS_OK);
    CHECK(test_boot() == CRUX_CREDENTIAL_OK);
    crux_file_fake_fail(CRUX_FILE_FAKE_READ, 0, CRUX_FILE_ERR_IO);
    crux_access_test_reset();
    CHECK(crux_access_init(&policy) == CRUX_ACCESS_ERR_NOT_READY);
    CHECK(provision() == CRUX_ACCESS_ERR_DENIED);
}

/* AC8: a failed save promises only that RAM is unchanged. */
static void test_failed_save_keeps_ram(void)
{
    for (int invalidation_fails = 0; invalidation_fails < 2; ++invalidation_fails) {
        test_fresh();
        CHECK(boot_both() == CRUX_ACCESS_OK);
        CHECK(provision() == CRUX_ACCESS_OK);
        crux_access_session_t session = {0};
        CHECK(login(&session) == CRUX_ACCESS_OK);

        crux_file_fake_fail(CRUX_FILE_FAKE_SYNC, 0, CRUX_FILE_ERR_IO);
        if (invalidation_fails) {
            /* The load and the write used two opens, and the third is invalidation's. */
            crux_file_fake_fail(CRUX_FILE_FAKE_OPEN, 2, CRUX_FILE_ERR_IO);
        }
        CHECK(crux_access_account_create(session, OTHER, "administrator", PASSWORD,
                                         strlen(PASSWORD)) == CRUX_ACCESS_ERR_STORE);
        /* RAM is unchanged: the new account does not exist, and the session goes on. */
        crux_access_account_info_t accounts[2];
        size_t count = 0;
        CHECK(crux_access_account_list(session, accounts, 2, &count) == CRUX_ACCESS_OK);
        CHECK(count == 1u);
        CHECK(crux_access_check(session, CRUX_ACCESS_PERMISSION_MANAGE) == CRUX_ACCESS_OK);

        /* What the medium holds at the next boot depends on the invalidation. */
        CHECK(boot_both() == CRUX_ACCESS_OK);
        session.value = 0;
        CHECK(login(&session) == CRUX_ACCESS_OK);
        CHECK(crux_access_account_list(session, accounts, 2, &count) == CRUX_ACCESS_OK);
        CHECK(count == (invalidation_fails ? 2u : 1u));
    }
}

/* S7: the first account's second write fails. Access reports the failure and stays open in RAM, and
 * the account is on the medium, so a reset loads it and closes provisioning. */
static void test_failed_second_write(void)
{
    test_fresh();
    CHECK(boot_both() == CRUX_ACCESS_OK);
    /* Slot A takes the image and the real header, and slot B's image is the third write. */
    crux_file_fake_fail(CRUX_FILE_FAKE_WRITE, 2, CRUX_FILE_ERR_IO);
    CHECK(provision() == CRUX_ACCESS_ERR_STORE);
    CHECK(crux_credential_state() == CRUX_CREDENTIAL_STATE_VALID);
    /* Access's RAM is unchanged: no account, and provisioning stays open. */
    crux_access_session_t session = {0};
    CHECK(login(&session) == CRUX_ACCESS_ERR_DENIED);
    CHECK(provisioning_open());

    /* Its retry writes the table it holds, which completes the slots. */
    CHECK(provision() == CRUX_ACCESS_OK);
    CHECK(login(&session) == CRUX_ACCESS_OK);
    for (size_t damaged = TEST_SLOT_A; damaged <= TEST_SLOT_B; ++damaged) {
        static uint8_t saved[CRUX_CREDENTIAL_SLOT_BYTES];
        CHECK(test_slot_read(damaged, saved, sizeof(saved)) == CRUX_CREDENTIAL_SLOT_BYTES);
        test_slot_flip(damaged, CRUX_CREDENTIAL_HEADER_BYTES, 0x01);
        CHECK(boot_both() == CRUX_ACCESS_OK);
        CHECK(login(&session) == CRUX_ACCESS_OK);
        test_slot_write(damaged, saved, sizeof(saved));
    }

    /* Without a retry, a reset loads the account the failed call left active. */
    test_fresh();
    CHECK(boot_both() == CRUX_ACCESS_OK);
    crux_file_fake_fail(CRUX_FILE_FAKE_WRITE, 2, CRUX_FILE_ERR_IO);
    CHECK(provision() == CRUX_ACCESS_ERR_STORE);
    CHECK(boot_both() == CRUX_ACCESS_OK);
    CHECK(login(&session) == CRUX_ACCESS_OK);
    CHECK(provision() == CRUX_ACCESS_ERR_DENIED);
    CHECK(!provisioning_open());
}

/* A cut during the first account's save: absent or present, and never a closed store with neither.
 */
static void test_cut_during_first_account(void)
{
    uint32_t total = 0;
    test_fresh();
    crux_file_fake_write_through(512);
    CHECK(boot_both() == CRUX_ACCESS_OK);
    uint32_t before = crux_file_fake_mutations();
    CHECK(provision() == CRUX_ACCESS_OK);
    total = crux_file_fake_mutations() - before;
    CHECK(total > 0);
    for (uint32_t cut = 0; cut <= total; ++cut) {
        (void)snprintf(credential_test_context, sizeof(credential_test_context), "cut %u of %u",
                       (unsigned)cut, (unsigned)total);
        test_fresh();
        crux_file_fake_write_through(512);
        CHECK(boot_both() == CRUX_ACCESS_OK);
        crux_file_fake_cut_after(cut, NULL);
        (void)provision();
        crux_file_fake_power_on();
        CHECK(boot_both() == CRUX_ACCESS_OK);
        crux_access_session_t session = {0};
        if (login(&session) == CRUX_ACCESS_OK) {
            CHECK(provision() == CRUX_ACCESS_ERR_DENIED);
        } else {
            /* Absent, and the operator repeats it. */
            CHECK(cut < total);
            CHECK(provision() == CRUX_ACCESS_OK);
            CHECK(login(&session) == CRUX_ACCESS_OK);
        }
    }
    credential_test_context[0] = '\0';
}

/* S6, accepted for the Pilot: losing both directory entries reads as EMPTY and reopens
 * provisioning. Only a marker outside the medium would close this, as limit A7 says. */
static void test_lost_directory_entries_reopen_provisioning(void)
{
    test_fresh();
    CHECK(boot_both() == CRUX_ACCESS_OK);
    CHECK(provision() == CRUX_ACCESS_OK);
    test_slot_remove(TEST_SLOT_A);
    test_slot_remove(TEST_SLOT_B);
    CHECK(boot_both() == CRUX_ACCESS_OK);
    CHECK(crux_credential_state() == CRUX_CREDENTIAL_STATE_EMPTY);
    crux_access_session_t session = {0};
    CHECK(login(&session) == CRUX_ACCESS_ERR_DENIED);
    CHECK(provision() == CRUX_ACCESS_OK);

    /* The erase path reaches EMPTY only at the next reset, because Access acts on what it loaded.
     */
    CHECK(crux_credential_erase() == CRUX_CREDENTIAL_OK);
    CHECK(crux_credential_write(CRUX_CREDENTIAL_RECORD_ACCOUNTS, PASSWORD, strlen(PASSWORD)) ==
          CRUX_CREDENTIAL_ERR_UNUSABLE);
    CHECK(boot_both() == CRUX_ACCESS_OK);
    CHECK(login(&session) == CRUX_ACCESS_ERR_DENIED);
    CHECK(provision() == CRUX_ACCESS_OK);
}

/* An accepted limit: a removed account comes back if the newest slot rots. A6 allows the old
 * record, and the event makes it visible. */
static void test_rotted_newest_slot_restores_an_account(void)
{
    test_fresh();
    CHECK(boot_both() == CRUX_ACCESS_OK);
    CHECK(provision() == CRUX_ACCESS_OK);
    crux_access_session_t session = {0};
    CHECK(login(&session) == CRUX_ACCESS_OK);
    CHECK(crux_access_account_create(session, OTHER, "administrator", PASSWORD, strlen(PASSWORD)) ==
          CRUX_ACCESS_OK);
    uint64_t generation_a = 0;
    uint64_t generation_b = 0;
    CHECK(test_slot_generation(TEST_SLOT_A, &generation_a));
    CHECK(test_slot_generation(TEST_SLOT_B, &generation_b));
    CHECK(crux_access_account_remove(session, OTHER) == CRUX_ACCESS_OK);
    uint64_t newer_a = 0;
    uint64_t newer_b = 0;
    CHECK(test_slot_generation(TEST_SLOT_A, &newer_a));
    CHECK(test_slot_generation(TEST_SLOT_B, &newer_b));
    size_t newest = newer_a > newer_b ? TEST_SLOT_A : TEST_SLOT_B;
    test_slot_flip(newest, CRUX_CREDENTIAL_HEADER_BYTES, 0x01);
    credential_fake_events_clear();
    CHECK(boot_both() == CRUX_ACCESS_OK);
    CHECK(credential_fake_slot_invalid_count(newest == TEST_SLOT_A ? 'a' : 'b') == 1u);
    session.value = 0;
    CHECK(login(&session) == CRUX_ACCESS_OK);
    crux_access_account_info_t accounts[2];
    size_t count = 0;
    CHECK(crux_access_account_list(session, accounts, 2, &count) == CRUX_ACCESS_OK);
    CHECK(count == 2u);
}

/* The bridge alone: what each store result reads as, and that load writes nothing unless VALID. */
static void test_bridge_mapping(void)
{
    uint8_t record[16];
    size_t length = 77;

    test_fresh();
    memset(record, 0xEE, sizeof(record));
    CHECK(crux_access_port_store_load(record, sizeof(record), &length) ==
          CRUX_ACCESS_STORE_UNUSABLE);
    CHECK(record[0] == 0xEE && length == 77u);
    CHECK(!crux_access_port_store_save(record, sizeof(record)));

    CHECK(test_boot() == CRUX_CREDENTIAL_OK);
    CHECK(crux_access_port_store_load(record, sizeof(record), &length) == CRUX_ACCESS_STORE_EMPTY);
    CHECK(record[0] == 0xEE && length == 77u);
    const uint8_t value[] = "accounts";
    CHECK(crux_access_port_store_save(value, sizeof(value)));
    CHECK(crux_access_port_store_load(record, sizeof(record), &length) == CRUX_ACCESS_STORE_VALID);
    CHECK(length == sizeof(value) && memcmp(record, value, sizeof(value)) == 0);

    /* A record too big for the caller's buffer is not absent. */
    uint8_t tiny[2] = {0xEE, 0xEE};
    length = 77;
    CHECK(crux_access_port_store_load(tiny, sizeof(tiny), &length) == CRUX_ACCESS_STORE_UNUSABLE);
    CHECK(tiny[0] == 0xEE && length == 77u);

    /* An UNUSABLE store. */
    test_slot_flip(TEST_SLOT_A, CRUX_CREDENTIAL_HEADER_BYTES, 0x01);
    test_slot_flip(TEST_SLOT_B, CRUX_CREDENTIAL_HEADER_BYTES, 0x01);
    CHECK(test_boot() == CRUX_CREDENTIAL_ERR_UNUSABLE);
    CHECK(crux_access_port_store_load(record, sizeof(record), &length) ==
          CRUX_ACCESS_STORE_UNUSABLE);
    CHECK(!crux_access_port_store_save(value, sizeof(value)));
}

int main(void)
{
    test_keys_do_not_close_provisioning();
    test_store_that_did_not_mount();
    test_store_not_started();
    test_corrupt_stores_stay_closed();
    test_failed_save_keeps_ram();
    test_failed_second_write();
    test_cut_during_first_account();
    test_lost_directory_entries_reopen_provisioning();
    test_rotted_newest_slot_restores_an_account();
    test_bridge_mapping();
    return credential_test_finish("credential store under crux_access");
}
