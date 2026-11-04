/*
 * The store's contract, on any File provider. A fault that needs a fake is in faults.c. Host tests
 * show contract behaviour: they do not establish confidentiality, entropy or real power loss.
 */
#include "conformance.h"

#include "credential_test.h"

static uint8_t image_a[CRUX_CREDENTIAL_SLOT_BYTES];
static uint8_t image_b[CRUX_CREDENTIAL_SLOT_BYTES];
static uint8_t large[CRUX_CREDENTIAL_SLOT_BYTES];

static const uint8_t accounts_one[] = "accounts, version one";
static const uint8_t accounts_two[] = "accounts, version two, which is longer";
static const uint8_t certificate[] = "certificate bytes";

static const test_record_t record_accounts_one = {CRUX_CREDENTIAL_RECORD_ACCOUNTS, accounts_one,
                                                  sizeof(accounts_one)};
static const test_record_t record_accounts_two = {CRUX_CREDENTIAL_RECORD_ACCOUNTS, accounts_two,
                                                  sizeof(accounts_two)};
static const test_record_t record_certificate = {CRUX_CREDENTIAL_RECORD_TLS_CERTIFICATE,
                                                 certificate, sizeof(certificate)};

static void expect_absent(crux_credential_record_t record)
{
    uint8_t buffer[8];
    size_t length = 99;
    CHECK(crux_credential_read(record, buffer, sizeof(buffer), &length) ==
          CRUX_CREDENTIAL_ERR_NOT_FOUND);
    CHECK(length == 0);
}

/* Every operation that needs a usable store refuses, and nothing reaches the medium. */
static void expect_refuses(crux_credential_result_t refusal)
{
    uint8_t buffer[8];
    size_t length = 99;
    CHECK(crux_credential_read(CRUX_CREDENTIAL_RECORD_ACCOUNTS, buffer, sizeof(buffer), &length) ==
          refusal);
    CHECK(length == 0);
    CHECK(crux_credential_write(CRUX_CREDENTIAL_RECORD_ACCOUNTS, accounts_one,
                                sizeof(accounts_one)) == refusal);
    unsigned generated = credential_fake_keygen_count();
    CHECK(crux_credential_key_ensure(CRUX_CREDENTIAL_RECORD_SSH_HOST_KEY,
                                     CRUX_CREDENTIAL_KEY_ED25519) == refusal);
    CHECK(crux_credential_key_replace(CRUX_CREDENTIAL_RECORD_SSH_HOST_KEY,
                                      CRUX_CREDENTIAL_KEY_ED25519) == refusal);
    CHECK(crux_credential_key_public(CRUX_CREDENTIAL_RECORD_SSH_HOST_KEY, buffer, sizeof(buffer),
                                     &length) == refusal);
    CHECK(credential_fake_keygen_count() == generated);
}

static void test_before_start(void)
{
    test_fresh();
    crux_credential_store_info_t info = {.state = CRUX_CREDENTIAL_STATE_VALID};
    crux_file_root_t roots[2] = {9, 9};
    CHECK(crux_credential_state() == CRUX_CREDENTIAL_STATE_UNUSABLE);
    CHECK(crux_credential_store_get_info(&info) == CRUX_ERR_NOT_READY);
    CHECK(info.state == CRUX_CREDENTIAL_STATE_VALID);
    CHECK(crux_credential_roots(roots) == CRUX_CREDENTIAL_ERR_NOT_READY);
    CHECK(roots[0] == 9 && roots[1] == 9);
    expect_refuses(CRUX_CREDENTIAL_ERR_NOT_READY);
    CHECK(crux_credential_erase() == CRUX_CREDENTIAL_ERR_NOT_READY);
    CHECK(!test_slot_exists(TEST_SLOT_A) && !test_slot_exists(TEST_SLOT_B));
}

static void test_start_arguments(void)
{
    test_fresh();
    crux_file_root_t roots[2] = {0, 0};
    CHECK(crux_credential_start(TEST_ROOT_A, TEST_ROOT_A) == CRUX_CREDENTIAL_ERR_INVALID_ARGUMENT);
    CHECK(crux_credential_state() == CRUX_CREDENTIAL_STATE_UNUSABLE);
    CHECK(crux_credential_roots(roots) == CRUX_CREDENTIAL_ERR_NOT_READY);

    CHECK(crux_credential_start(TEST_ROOT_A, TEST_ROOT_B) == CRUX_CREDENTIAL_OK);
    CHECK(crux_credential_roots(NULL) == CRUX_CREDENTIAL_ERR_INVALID_ARGUMENT);
    CHECK(crux_credential_roots(roots) == CRUX_CREDENTIAL_OK);
    CHECK(roots[0] == TEST_ROOT_A && roots[1] == TEST_ROOT_B);

    /* A repeated start changes nothing, even when the medium has changed under it. */
    CHECK(crux_credential_write(CRUX_CREDENTIAL_RECORD_ACCOUNTS, accounts_one,
                                sizeof(accounts_one)) == CRUX_CREDENTIAL_OK);
    CHECK(crux_credential_start(TEST_ROOT_A, TEST_ROOT_B) == CRUX_CREDENTIAL_ERR_INVALID_ARGUMENT);
    CHECK(crux_credential_state() == CRUX_CREDENTIAL_STATE_VALID);
    test_expect_record(CRUX_CREDENTIAL_RECORD_ACCOUNTS, accounts_one, sizeof(accounts_one));
}

/* What one slot of the load table can hold. */
typedef enum {
    KIND_ABSENT,
    KIND_ZERO_BYTES,
    KIND_SHORT,
    KIND_PENDING,
    KIND_VALID,
    KIND_BAD_TAG,
    KIND_ALL_ZEROS,
    KIND_NEW_FORMAT,
    KIND_COUNT
} slot_kind_t;

static bool kind_is_corrupt(slot_kind_t kind)
{
    return kind == KIND_BAD_TAG || kind == KIND_ALL_ZEROS || kind == KIND_NEW_FORMAT;
}

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters): every helper takes the slot first.
static void slot_prepare(size_t slot, slot_kind_t kind)
{
    switch (kind) {
    case KIND_ABSENT:
        test_slot_remove(slot);
        break;
    case KIND_ZERO_BYTES:
        test_slot_write(slot, large, 0);
        break;
    case KIND_SHORT:
        memset(large, 0x5A, CRUX_CREDENTIAL_HEADER_BYTES);
        test_slot_write(slot, large, CRUX_CREDENTIAL_HEADER_BYTES - 1u);
        break;
    case KIND_PENDING:
        memset(large, 0, sizeof(large));
        crux_credential_header_encode_pending(large);
        test_slot_write(slot, large, sizeof(large));
        break;
    case KIND_VALID:
        test_image_build(large, 3, &record_accounts_one, 1);
        test_slot_write(slot, large, sizeof(large));
        break;
    case KIND_BAD_TAG:
        test_image_build(large, 3, &record_accounts_one, 1);
        large[CRUX_CREDENTIAL_HEADER_BYTES + 1u] ^= 0x01u;
        test_slot_write(slot, large, sizeof(large));
        break;
    case KIND_ALL_ZEROS:
        memset(large, 0, sizeof(large));
        test_slot_write(slot, large, sizeof(large));
        break;
    case KIND_NEW_FORMAT: {
        uint8_t payload[CRUX_CREDENTIAL_TABLE_HEADER_BYTES];
        size_t length = crux_credential_payload_empty(payload);
        test_image_seal(large, 3, CRUX_CREDENTIAL_FORMAT + 1u, (uint16_t)CRUX_CREDENTIAL_BASIS_NONE,
                        payload, length);
        test_slot_write(slot, large, sizeof(large));
        break;
    }
    case KIND_COUNT:
        break;
    }
}

/* Each row of the load table, in both orders, which also shows that the table is symmetric. */
static void test_load_table(void)
{
    for (int a = 0; a < KIND_COUNT; ++a) {
        for (int b = 0; b < KIND_COUNT; ++b) {
            test_fresh();
            (void)snprintf(credential_test_context, sizeof(credential_test_context),
                           "slot A kind %d, slot B kind %d", a, b);
            slot_prepare(TEST_SLOT_A, (slot_kind_t)a);
            slot_prepare(TEST_SLOT_B, (slot_kind_t)b);
            crux_credential_result_t started = test_boot();
            crux_credential_store_info_t info;
            CHECK(crux_credential_store_get_info(&info) == CRUX_OK);
            CHECK(info.state == crux_credential_state());
            size_t invalid_a = credential_fake_slot_invalid_count('a');
            size_t invalid_b = credential_fake_slot_invalid_count('b');
            CHECK(invalid_a == (kind_is_corrupt((slot_kind_t)a) ? 1u : 0u));
            CHECK(invalid_b == (kind_is_corrupt((slot_kind_t)b) ? 1u : 0u));
            if (a == KIND_VALID || b == KIND_VALID) {
                CHECK(started == CRUX_CREDENTIAL_OK);
                CHECK(info.state == CRUX_CREDENTIAL_STATE_VALID);
                test_expect_record(CRUX_CREDENTIAL_RECORD_ACCOUNTS, accounts_one,
                                   sizeof(accounts_one));
            } else if (kind_is_corrupt((slot_kind_t)a) || kind_is_corrupt((slot_kind_t)b)) {
                /* Requirement A6: a corrupt record never opens provisioning. */
                CHECK(started == CRUX_CREDENTIAL_ERR_UNUSABLE);
                CHECK(info.state == CRUX_CREDENTIAL_STATE_UNUSABLE);
                expect_refuses(CRUX_CREDENTIAL_ERR_UNUSABLE);
            } else {
                CHECK(started == CRUX_CREDENTIAL_OK);
                CHECK(info.state == CRUX_CREDENTIAL_STATE_EMPTY);
                expect_absent(CRUX_CREDENTIAL_RECORD_ACCOUNTS);
            }
            CHECK(crux_credential_test_buffers_clear());
        }
    }
}

/* An unbound root is what a card that did not mount gives. It is never EMPTY, and a good slot
 * beside it cannot be trusted to be the newest. */
static void test_unreadable_root(void)
{
    test_fresh();
    CHECK(test_boot() == CRUX_CREDENTIAL_OK);
    CHECK(crux_credential_write(CRUX_CREDENTIAL_RECORD_ACCOUNTS, accounts_one,
                                sizeof(accounts_one)) == CRUX_CREDENTIAL_OK);

    crux_credential_test_reset();
    CHECK(crux_credential_start(TEST_ROOT_A, TEST_ROOT_UNBOUND) == CRUX_CREDENTIAL_ERR_UNUSABLE);
    CHECK(crux_credential_state() == CRUX_CREDENTIAL_STATE_UNUSABLE);
    expect_refuses(CRUX_CREDENTIAL_ERR_UNUSABLE);

    crux_credential_test_reset();
    CHECK(crux_credential_start(TEST_ROOT_UNBOUND, TEST_ROOT_B) == CRUX_CREDENTIAL_ERR_UNUSABLE);
    CHECK(crux_credential_state() == CRUX_CREDENTIAL_STATE_UNUSABLE);

    /* No medium at all: both roots unbound, with nothing to show that this is not a first boot. */
    test_fresh();
    crux_credential_test_reset();
    CHECK(crux_credential_start(TEST_ROOT_UNBOUND, TEST_ROOT_UNBOUND - 1) ==
          CRUX_CREDENTIAL_ERR_UNUSABLE);
    CHECK(crux_credential_state() == CRUX_CREDENTIAL_STATE_UNUSABLE);
    expect_refuses(CRUX_CREDENTIAL_ERR_UNUSABLE);

    /* It lasts until the next reset, whatever the medium does meanwhile. */
    CHECK(crux_credential_start(TEST_ROOT_A, TEST_ROOT_B) == CRUX_CREDENTIAL_ERR_INVALID_ARGUMENT);
    CHECK(crux_credential_state() == CRUX_CREDENTIAL_STATE_UNUSABLE);
}

static void test_records_read_write(void)
{
    test_fresh_empty();
    uint8_t key_tls[CRUX_CREDENTIAL_KEY_RECORD_BYTES_MAX];
    uint8_t key_ssh[CRUX_CREDENTIAL_KEY_RECORD_BYTES_MAX];
    size_t key_tls_length = test_key_record(CRUX_CREDENTIAL_KEY_ECDSA_P256, key_tls, 0x11);
    size_t key_ssh_length = test_key_record(CRUX_CREDENTIAL_KEY_ED25519, key_ssh, 0x22);

    expect_absent(CRUX_CREDENTIAL_RECORD_ACCOUNTS);
    CHECK(crux_credential_write(CRUX_CREDENTIAL_RECORD_ACCOUNTS, accounts_one,
                                sizeof(accounts_one)) == CRUX_CREDENTIAL_OK);
    CHECK(crux_credential_state() == CRUX_CREDENTIAL_STATE_VALID);
    CHECK(crux_credential_test_buffers_clear());
    expect_absent(CRUX_CREDENTIAL_RECORD_TLS_KEY);
    CHECK(crux_credential_write(CRUX_CREDENTIAL_RECORD_TLS_KEY, key_tls, key_tls_length) ==
          CRUX_CREDENTIAL_OK);
    CHECK(crux_credential_write(CRUX_CREDENTIAL_RECORD_TLS_CERTIFICATE, certificate,
                                sizeof(certificate)) == CRUX_CREDENTIAL_OK);
    CHECK(crux_credential_write(CRUX_CREDENTIAL_RECORD_SSH_HOST_KEY, key_ssh, key_ssh_length) ==
          CRUX_CREDENTIAL_OK);
    CHECK(crux_credential_write(CRUX_CREDENTIAL_RECORD_ACCOUNTS, accounts_two,
                                sizeof(accounts_two)) == CRUX_CREDENTIAL_OK);

    for (int boot = 0; boot < 2; ++boot) {
        test_expect_record(CRUX_CREDENTIAL_RECORD_ACCOUNTS, accounts_two, sizeof(accounts_two));
        test_expect_record(CRUX_CREDENTIAL_RECORD_TLS_KEY, key_tls, key_tls_length);
        test_expect_record(CRUX_CREDENTIAL_RECORD_TLS_CERTIFICATE, certificate,
                           sizeof(certificate));
        test_expect_record(CRUX_CREDENTIAL_RECORD_SSH_HOST_KEY, key_ssh, key_ssh_length);
        CHECK(crux_credential_test_buffers_clear());
        CHECK(test_boot() == CRUX_CREDENTIAL_OK);
        CHECK(crux_credential_state() == CRUX_CREDENTIAL_STATE_VALID);
    }

    /* A record larger than the buffer reports its size and copies nothing. */
    uint8_t small[8];
    memset(small, 0xEE, sizeof(small));
    size_t length = 0;
    CHECK(crux_credential_read(CRUX_CREDENTIAL_RECORD_ACCOUNTS, small, sizeof(small), &length) ==
          CRUX_CREDENTIAL_ERR_TOO_LARGE);
    CHECK(length == sizeof(accounts_two));
    CHECK(small[0] == 0xEE);
    CHECK(crux_credential_read(CRUX_CREDENTIAL_RECORD_ACCOUNTS, NULL, 0, &length) ==
          CRUX_CREDENTIAL_ERR_TOO_LARGE);
    CHECK(length == sizeof(accounts_two));

    /* Malformed arguments, which refuse before anything else is looked at. */
    // NOLINTNEXTLINE(clang-analyzer-optin.core.EnumCastOutOfRange)
    const crux_credential_record_t record_zero = (crux_credential_record_t)0;
    // NOLINTNEXTLINE(clang-analyzer-optin.core.EnumCastOutOfRange)
    const crux_credential_record_t record_five = (crux_credential_record_t)5;
    CHECK(crux_credential_read(record_zero, small, sizeof(small), &length) ==
          CRUX_CREDENTIAL_ERR_INVALID_ARGUMENT);
    CHECK(crux_credential_read(record_five, small, sizeof(small), &length) ==
          CRUX_CREDENTIAL_ERR_INVALID_ARGUMENT);
    CHECK(crux_credential_read(CRUX_CREDENTIAL_RECORD_ACCOUNTS, small, sizeof(small), NULL) ==
          CRUX_CREDENTIAL_ERR_INVALID_ARGUMENT);
    CHECK(crux_credential_read(CRUX_CREDENTIAL_RECORD_ACCOUNTS, NULL, 8, &length) ==
          CRUX_CREDENTIAL_ERR_INVALID_ARGUMENT);
    CHECK(crux_credential_write(record_five, small, sizeof(small)) ==
          CRUX_CREDENTIAL_ERR_INVALID_ARGUMENT);
    CHECK(crux_credential_write(CRUX_CREDENTIAL_RECORD_ACCOUNTS, NULL, 8) ==
          CRUX_CREDENTIAL_ERR_INVALID_ARGUMENT);
    CHECK(crux_credential_write(CRUX_CREDENTIAL_RECORD_ACCOUNTS, small, 0) ==
          CRUX_CREDENTIAL_ERR_INVALID_ARGUMENT);

    /* A malformed key record cannot be stored, so crux_credential_key_public can always split one.
     */
    CHECK(crux_credential_write(CRUX_CREDENTIAL_RECORD_TLS_KEY, accounts_one,
                                sizeof(accounts_one)) == CRUX_CREDENTIAL_ERR_INVALID_ARGUMENT);
    test_expect_record(CRUX_CREDENTIAL_RECORD_TLS_KEY, key_tls, key_tls_length);
    test_expect_record(CRUX_CREDENTIAL_RECORD_ACCOUNTS, accounts_two, sizeof(accounts_two));
}

static void test_limits(void)
{
    test_fresh_empty();
    uint8_t key[CRUX_CREDENTIAL_KEY_RECORD_BYTES_MAX];
    size_t key_length = test_key_record(CRUX_CREDENTIAL_KEY_ECDSA_P256, key, 0x33);
    static uint8_t accounts[CRUX_CREDENTIAL_ACCOUNTS_BYTES_MAX + 1u];
    static uint8_t certificate_max[CRUX_CREDENTIAL_CERTIFICATE_BYTES_MAX + 1u];
    memset(accounts, 0x41, sizeof(accounts));
    memset(certificate_max, 0x42, sizeof(certificate_max));

    /* Over a record's limit is refused before anything is written. */
    CHECK(crux_credential_write(CRUX_CREDENTIAL_RECORD_ACCOUNTS, accounts, sizeof(accounts)) ==
          CRUX_CREDENTIAL_ERR_TOO_LARGE);
    CHECK(crux_credential_write(CRUX_CREDENTIAL_RECORD_TLS_CERTIFICATE, certificate_max,
                                sizeof(certificate_max)) == CRUX_CREDENTIAL_ERR_TOO_LARGE);
    CHECK(crux_credential_write(CRUX_CREDENTIAL_RECORD_TLS_KEY, key,
                                CRUX_CREDENTIAL_KEY_RECORD_BYTES_MAX + 1u) ==
          CRUX_CREDENTIAL_ERR_TOO_LARGE);
    CHECK(crux_credential_state() == CRUX_CREDENTIAL_STATE_EMPTY);
    CHECK(!test_slot_exists(TEST_SLOT_A) && !test_slot_exists(TEST_SLOT_B));

    /* At the default slot size, every record at its limit fits together. */
    CHECK(crux_credential_write(CRUX_CREDENTIAL_RECORD_ACCOUNTS, accounts, sizeof(accounts) - 1u) ==
          CRUX_CREDENTIAL_OK);
    CHECK(crux_credential_write(CRUX_CREDENTIAL_RECORD_TLS_CERTIFICATE, certificate_max,
                                sizeof(certificate_max) - 1u) == CRUX_CREDENTIAL_OK);
    CHECK(crux_credential_write(CRUX_CREDENTIAL_RECORD_TLS_KEY, key, key_length) ==
          CRUX_CREDENTIAL_OK);
    CHECK(crux_credential_write(CRUX_CREDENTIAL_RECORD_SSH_HOST_KEY, key, key_length) ==
          CRUX_CREDENTIAL_OK);
    CHECK(test_boot() == CRUX_CREDENTIAL_OK);
    test_expect_record(CRUX_CREDENTIAL_RECORD_ACCOUNTS, accounts, sizeof(accounts) - 1u);
    test_expect_record(CRUX_CREDENTIAL_RECORD_TLS_CERTIFICATE, certificate_max,
                       sizeof(certificate_max) - 1u);
}

static void test_generation_order_and_ties(void)
{
    /* The higher generation wins, whichever slot holds it. */
    test_fresh();
    test_image_build(image_a, 5, &record_accounts_one, 1);
    test_image_build(image_b, 7, &record_accounts_two, 1);
    test_slot_write(TEST_SLOT_A, image_a, sizeof(image_a));
    test_slot_write(TEST_SLOT_B, image_b, sizeof(image_b));
    CHECK(test_boot() == CRUX_CREDENTIAL_OK);
    test_expect_record(CRUX_CREDENTIAL_RECORD_ACCOUNTS, accounts_two, sizeof(accounts_two));
    test_slot_write(TEST_SLOT_A, image_b, sizeof(image_b));
    test_slot_write(TEST_SLOT_B, image_a, sizeof(image_a));
    CHECK(test_boot() == CRUX_CREDENTIAL_OK);
    test_expect_record(CRUX_CREDENTIAL_RECORD_ACCOUNTS, accounts_two, sizeof(accounts_two));

    /* A tie takes slot A, and generations are compared across their whole range. */
    test_image_build(image_a, 0x100000000ull, &record_accounts_one, 1);
    test_image_build(image_b, 0x100000000ull, &record_accounts_two, 1);
    test_slot_write(TEST_SLOT_A, image_a, sizeof(image_a));
    test_slot_write(TEST_SLOT_B, image_b, sizeof(image_b));
    CHECK(test_boot() == CRUX_CREDENTIAL_OK);
    test_expect_record(CRUX_CREDENTIAL_RECORD_ACCOUNTS, accounts_one, sizeof(accounts_one));

    /* A save goes to the slot that is not active, one generation up. */
    CHECK(crux_credential_write(CRUX_CREDENTIAL_RECORD_ACCOUNTS, accounts_two,
                                sizeof(accounts_two)) == CRUX_CREDENTIAL_OK);
    uint64_t generation_a = 0;
    uint64_t generation_b = 0;
    CHECK(test_slot_generation(TEST_SLOT_A, &generation_a));
    CHECK(test_slot_generation(TEST_SLOT_B, &generation_b));
    CHECK(generation_a == 0x100000000ull);
    CHECK(generation_b == 0x100000001ull);

    /* The next save takes the other slot. */
    CHECK(crux_credential_write(CRUX_CREDENTIAL_RECORD_ACCOUNTS, accounts_one,
                                sizeof(accounts_one)) == CRUX_CREDENTIAL_OK);
    CHECK(test_slot_generation(TEST_SLOT_A, &generation_a));
    CHECK(test_slot_generation(TEST_SLOT_B, &generation_b));
    CHECK(generation_a == 0x100000002ull);
    CHECK(generation_b == 0x100000001ull);
    test_expect_record(CRUX_CREDENTIAL_RECORD_ACCOUNTS, accounts_one, sizeof(accounts_one));

    /* The first save writes the same image to both, which is a tie. */
    test_fresh_empty();
    CHECK(crux_credential_write(CRUX_CREDENTIAL_RECORD_ACCOUNTS, accounts_one,
                                sizeof(accounts_one)) == CRUX_CREDENTIAL_OK);
    CHECK(test_slot_generation(TEST_SLOT_A, &generation_a));
    CHECK(test_slot_generation(TEST_SLOT_B, &generation_b));
    CHECK(generation_a == generation_b);
    CHECK(test_slot_read(TEST_SLOT_A, image_a, sizeof(image_a)) == CRUX_CREDENTIAL_SLOT_BYTES);
    CHECK(test_slot_read(TEST_SLOT_B, image_b, sizeof(image_b)) == CRUX_CREDENTIAL_SLOT_BYTES);
    CHECK(memcmp(image_a, image_b, sizeof(image_a)) == 0);

    /* The last possible generation cannot be followed, so the store refuses to wrap back to old. */
    test_fresh();
    test_image_build(image_a, UINT64_MAX, &record_accounts_one, 1);
    test_slot_write(TEST_SLOT_A, image_a, sizeof(image_a));
    CHECK(test_boot() == CRUX_CREDENTIAL_OK);
    CHECK(crux_credential_write(CRUX_CREDENTIAL_RECORD_ACCOUNTS, accounts_two,
                                sizeof(accounts_two)) == CRUX_CREDENTIAL_ERR_STORE);
    test_expect_record(CRUX_CREDENTIAL_RECORD_ACCOUNTS, accounts_one, sizeof(accounts_one));
    CHECK(!test_slot_exists(TEST_SLOT_B));

    /* One generation short of the end: an added record reaches the first slot, and stops there. */
    test_fresh();
    test_image_build(image_a, UINT64_MAX - 1u, &record_accounts_one, 1);
    test_slot_write(TEST_SLOT_A, image_a, sizeof(image_a));
    test_slot_write(TEST_SLOT_B, image_a, sizeof(image_a));
    CHECK(test_boot() == CRUX_CREDENTIAL_OK);
    CHECK(crux_credential_write(CRUX_CREDENTIAL_RECORD_TLS_CERTIFICATE, certificate,
                                sizeof(certificate)) == CRUX_CREDENTIAL_ERR_STORE);
    test_expect_record(CRUX_CREDENTIAL_RECORD_TLS_CERTIFICATE, certificate, sizeof(certificate));
    CHECK(test_slot_generation(TEST_SLOT_A, &generation_a));
    CHECK(generation_a == UINT64_MAX - 1u);
}

/* An acknowledged record is in both slots, so a fault in either one cannot make it absent. */
static void test_new_record_reaches_both_slots(void)
{
    for (size_t damaged = TEST_SLOT_A; damaged <= TEST_SLOT_B; ++damaged) {
        test_fresh_empty();
        CHECK(crux_credential_write(CRUX_CREDENTIAL_RECORD_ACCOUNTS, accounts_one,
                                    sizeof(accounts_one)) == CRUX_CREDENTIAL_OK);
        CHECK(crux_credential_write(CRUX_CREDENTIAL_RECORD_TLS_CERTIFICATE, certificate,
                                    sizeof(certificate)) == CRUX_CREDENTIAL_OK);
        uint64_t generation_a = 0;
        uint64_t generation_b = 0;
        CHECK(test_slot_generation(TEST_SLOT_A, &generation_a));
        CHECK(test_slot_generation(TEST_SLOT_B, &generation_b));
        CHECK(generation_a != generation_b);

        test_slot_flip(damaged, CRUX_CREDENTIAL_HEADER_BYTES, 0x80);
        CHECK(test_boot() == CRUX_CREDENTIAL_OK);
        CHECK(crux_credential_state() == CRUX_CREDENTIAL_STATE_VALID);
        test_expect_record(CRUX_CREDENTIAL_RECORD_TLS_CERTIFICATE, certificate,
                           sizeof(certificate));
        test_expect_record(CRUX_CREDENTIAL_RECORD_ACCOUNTS, accounts_one, sizeof(accounts_one));
        CHECK(credential_fake_slot_invalid_count(damaged == TEST_SLOT_A ? 'a' : 'b') == 1u);

        /* The next save writes the damaged slot and clears the problem. */
        credential_fake_events_clear();
        CHECK(crux_credential_write(CRUX_CREDENTIAL_RECORD_ACCOUNTS, accounts_two,
                                    sizeof(accounts_two)) == CRUX_CREDENTIAL_OK);
        CHECK(test_boot() == CRUX_CREDENTIAL_OK);
        CHECK(credential_fake_event_count_of(CRUX_CREDENTIAL_EVENT_SLOT_INVALID) == 0u);
        test_expect_record(CRUX_CREDENTIAL_RECORD_ACCOUNTS, accounts_two, sizeof(accounts_two));
    }
}

/* Replacing a record writes one slot, so the other keeps the old image across a cut. */
static void test_replacing_writes_one_slot(void)
{
    test_fresh_empty();
    CHECK(crux_credential_write(CRUX_CREDENTIAL_RECORD_ACCOUNTS, accounts_one,
                                sizeof(accounts_one)) == CRUX_CREDENTIAL_OK);
    CHECK(test_slot_read(TEST_SLOT_A, image_a, sizeof(image_a)) == CRUX_CREDENTIAL_SLOT_BYTES);
    CHECK(crux_credential_write(CRUX_CREDENTIAL_RECORD_ACCOUNTS, accounts_two,
                                sizeof(accounts_two)) == CRUX_CREDENTIAL_OK);
    CHECK(test_slot_read(TEST_SLOT_A, large, sizeof(large)) == CRUX_CREDENTIAL_SLOT_BYTES);
    CHECK(memcmp(image_a, large, sizeof(large)) == 0);
    CHECK(test_slot_read(TEST_SLOT_B, large, sizeof(large)) == CRUX_CREDENTIAL_SLOT_BYTES);
    CHECK(memcmp(image_a, large, sizeof(large)) != 0);
}

/* The state a failed second write leaves is closed at the next start, before damage can arrive. */
static void test_repair_at_start(void)
{
    const test_record_t both[] = {record_accounts_one, record_certificate};
    for (size_t older = TEST_SLOT_A; older <= TEST_SLOT_B; ++older) {
        size_t newer = older == TEST_SLOT_A ? TEST_SLOT_B : TEST_SLOT_A;
        test_fresh();
        test_image_build(image_a, 3, &record_accounts_one, 1);
        test_image_build(image_b, 4, both, 2);
        test_slot_write(older, image_a, sizeof(image_a));
        test_slot_write(newer, image_b, sizeof(image_b));
        CHECK(test_boot() == CRUX_CREDENTIAL_OK);
        CHECK(credential_fake_event_count_of(CRUX_CREDENTIAL_EVENT_SAVE_FAILED) == 0u);
        test_expect_record(CRUX_CREDENTIAL_RECORD_TLS_CERTIFICATE, certificate,
                           sizeof(certificate));
        CHECK(test_slot_read(older, large, sizeof(large)) == CRUX_CREDENTIAL_SLOT_BYTES);
        CHECK(memcmp(large, image_b, sizeof(large)) == 0);
        CHECK(crux_credential_test_buffers_clear());

        test_slot_flip(newer, CRUX_CREDENTIAL_HEADER_BYTES, 0x80);
        CHECK(test_boot() == CRUX_CREDENTIAL_OK);
        test_expect_record(CRUX_CREDENTIAL_RECORD_TLS_CERTIFICATE, certificate,
                           sizeof(certificate));
    }

    /* An older slot that holds the same records has nothing to repair and is left alone. */
    test_fresh();
    test_image_build(image_a, 3, &record_accounts_one, 1);
    test_image_build(image_b, 4, &record_accounts_two, 1);
    test_slot_write(TEST_SLOT_A, image_a, sizeof(image_a));
    test_slot_write(TEST_SLOT_B, image_b, sizeof(image_b));
    CHECK(test_boot() == CRUX_CREDENTIAL_OK);
    CHECK(test_slot_read(TEST_SLOT_A, large, sizeof(large)) == CRUX_CREDENTIAL_SLOT_BYTES);
    CHECK(memcmp(large, image_a, sizeof(large)) == 0);

    /* Nor does the newer slot take a record only the older holds. That is limit A7. */
    test_fresh();
    test_image_build(image_a, 3, both, 2);
    test_image_build(image_b, 4, &record_accounts_two, 1);
    test_slot_write(TEST_SLOT_A, image_a, sizeof(image_a));
    test_slot_write(TEST_SLOT_B, image_b, sizeof(image_b));
    CHECK(test_boot() == CRUX_CREDENTIAL_OK);
    expect_absent(CRUX_CREDENTIAL_RECORD_TLS_CERTIFICATE);
    CHECK(test_slot_read(TEST_SLOT_A, large, sizeof(large)) == CRUX_CREDENTIAL_SLOT_BYTES);
    CHECK(memcmp(large, image_a, sizeof(large)) == 0);
}

static void test_unusable_refuses_all_but_erase(void)
{
    test_fresh();
    slot_prepare(TEST_SLOT_A, KIND_BAD_TAG);
    slot_prepare(TEST_SLOT_B, KIND_ALL_ZEROS);
    CHECK(test_boot() == CRUX_CREDENTIAL_ERR_UNUSABLE);
    CHECK(test_slot_read(TEST_SLOT_A, image_a, sizeof(image_a)) == CRUX_CREDENTIAL_SLOT_BYTES);
    CHECK(test_slot_read(TEST_SLOT_B, image_b, sizeof(image_b)) == CRUX_CREDENTIAL_SLOT_BYTES);
    expect_refuses(CRUX_CREDENTIAL_ERR_UNUSABLE);
    CHECK(credential_fake_keygen_count() == 0u);

    /* Nothing changed on the medium. */
    CHECK(test_slot_read(TEST_SLOT_A, large, sizeof(large)) == CRUX_CREDENTIAL_SLOT_BYTES);
    CHECK(memcmp(large, image_a, sizeof(large)) == 0);
    CHECK(test_slot_read(TEST_SLOT_B, large, sizeof(large)) == CRUX_CREDENTIAL_SLOT_BYTES);
    CHECK(memcmp(large, image_b, sizeof(large)) == 0);

    crux_file_root_t roots[2] = {0, 0};
    CHECK(crux_credential_roots(roots) == CRUX_CREDENTIAL_OK);
    CHECK(crux_credential_erase() == CRUX_CREDENTIAL_OK);
    CHECK(!test_slot_exists(TEST_SLOT_A) && !test_slot_exists(TEST_SLOT_B));

    /* Erase does not reopen provisioning until the next reset. */
    CHECK(crux_credential_state() == CRUX_CREDENTIAL_STATE_UNUSABLE);
    expect_refuses(CRUX_CREDENTIAL_ERR_UNUSABLE);
    CHECK(test_boot() == CRUX_CREDENTIAL_OK);
    CHECK(crux_credential_state() == CRUX_CREDENTIAL_STATE_EMPTY);
}

static void test_keys(void)
{
    uint8_t record[CRUX_CREDENTIAL_KEY_RECORD_BYTES_MAX];
    uint8_t public_key[CRUX_CREDENTIAL_KEY_PUBLIC_BYTES_MAX];
    uint8_t public_again[CRUX_CREDENTIAL_KEY_PUBLIC_BYTES_MAX];
    size_t length = 0;
    size_t public_length = 0;
    crux_credential_key_view_t view;

    /* Generated once, saved in both slots before it returns, and stable from then on. */
    test_fresh_empty();
    CHECK(crux_credential_key_ensure(CRUX_CREDENTIAL_RECORD_TLS_KEY,
                                     CRUX_CREDENTIAL_KEY_ECDSA_P256) == CRUX_CREDENTIAL_OK);
    CHECK(credential_fake_keygen_count() == 1u);
    CHECK(credential_fake_event_count_of(CRUX_CREDENTIAL_EVENT_KEY_GENERATED) == 1u);
    CHECK(crux_credential_state() == CRUX_CREDENTIAL_STATE_VALID);
    CHECK(crux_credential_test_buffers_clear());
    CHECK(crux_credential_key_public(CRUX_CREDENTIAL_RECORD_TLS_KEY, public_key, sizeof(public_key),
                                     &public_length) == CRUX_CREDENTIAL_OK);
    CHECK(public_length == 65u);
    CHECK(crux_credential_read(CRUX_CREDENTIAL_RECORD_TLS_KEY, record, sizeof(record), &length) ==
          CRUX_CREDENTIAL_OK);
    CHECK(crux_credential_key_parse(record, length, &view) == CRUX_CREDENTIAL_OK);
    CHECK(view.type == CRUX_CREDENTIAL_KEY_ECDSA_P256);
    CHECK(view.public_length == 65u && view.private_length == 32u);
    CHECK(memcmp(view.public_key, public_key, public_length) == 0);

    CHECK(crux_credential_key_ensure(CRUX_CREDENTIAL_RECORD_TLS_KEY,
                                     CRUX_CREDENTIAL_KEY_ECDSA_P256) == CRUX_CREDENTIAL_OK);
    CHECK(credential_fake_keygen_count() == 1u);
    CHECK(test_boot() == CRUX_CREDENTIAL_OK);
    CHECK(crux_credential_key_ensure(CRUX_CREDENTIAL_RECORD_TLS_KEY,
                                     CRUX_CREDENTIAL_KEY_ECDSA_P256) == CRUX_CREDENTIAL_OK);
    CHECK(credential_fake_keygen_count() == 1u);
    for (size_t damaged = TEST_SLOT_A; damaged <= TEST_SLOT_B; ++damaged) {
        CHECK(test_slot_read(damaged, large, sizeof(large)) == CRUX_CREDENTIAL_SLOT_BYTES);
        test_slot_flip(damaged, CRUX_CREDENTIAL_HEADER_BYTES, 0x80);
        CHECK(test_boot() == CRUX_CREDENTIAL_OK);
        CHECK(crux_credential_key_ensure(CRUX_CREDENTIAL_RECORD_TLS_KEY,
                                         CRUX_CREDENTIAL_KEY_ECDSA_P256) == CRUX_CREDENTIAL_OK);
        CHECK(credential_fake_keygen_count() == 1u);
        CHECK(crux_credential_key_public(CRUX_CREDENTIAL_RECORD_TLS_KEY, public_again,
                                         sizeof(public_again),
                                         &public_length) == CRUX_CREDENTIAL_OK);
        CHECK(memcmp(public_again, public_key, 65u) == 0);
        test_slot_write(damaged, large, sizeof(large));
    }

    /* An existing key of another type is left alone, and its type can be read from it. */
    CHECK(crux_credential_key_ensure(CRUX_CREDENTIAL_RECORD_TLS_KEY, CRUX_CREDENTIAL_KEY_ED25519) ==
          CRUX_CREDENTIAL_OK);
    CHECK(credential_fake_keygen_count() == 1u);

    /* The other key is another record, of the other size. */
    CHECK(crux_credential_key_ensure(CRUX_CREDENTIAL_RECORD_SSH_HOST_KEY,
                                     CRUX_CREDENTIAL_KEY_ED25519) == CRUX_CREDENTIAL_OK);
    CHECK(crux_credential_key_public(CRUX_CREDENTIAL_RECORD_SSH_HOST_KEY, public_key,
                                     sizeof(public_key), &public_length) == CRUX_CREDENTIAL_OK);
    CHECK(public_length == 32u);
    CHECK(crux_credential_read(CRUX_CREDENTIAL_RECORD_SSH_HOST_KEY, record, sizeof(record),
                               &length) == CRUX_CREDENTIAL_OK);
    CHECK(crux_credential_key_parse(record, length, &view) == CRUX_CREDENTIAL_OK);
    CHECK(view.type == CRUX_CREDENTIAL_KEY_ED25519);

    /* Replacing is the deliberate path, it says so, and the new key survives a reset. */
    credential_fake_events_clear();
    CHECK(crux_credential_key_replace(CRUX_CREDENTIAL_RECORD_SSH_HOST_KEY,
                                      CRUX_CREDENTIAL_KEY_ED25519) == CRUX_CREDENTIAL_OK);
    CHECK(credential_fake_event_count_of(CRUX_CREDENTIAL_EVENT_KEY_REPLACED) == 1u);
    CHECK(credential_fake_event_count_of(CRUX_CREDENTIAL_EVENT_KEY_GENERATED) == 0u);
    CHECK(crux_credential_key_public(CRUX_CREDENTIAL_RECORD_SSH_HOST_KEY, public_again,
                                     sizeof(public_again), &public_length) == CRUX_CREDENTIAL_OK);
    CHECK(memcmp(public_again, public_key, public_length) != 0);
    CHECK(test_boot() == CRUX_CREDENTIAL_OK);
    CHECK(crux_credential_key_public(CRUX_CREDENTIAL_RECORD_SSH_HOST_KEY, public_key,
                                     sizeof(public_key), &public_length) == CRUX_CREDENTIAL_OK);
    CHECK(memcmp(public_again, public_key, public_length) == 0);

    /* A key changed through a plain write is recorded the same way, and a new one is not a change.
     */
    credential_fake_events_clear();
    size_t imported_length = test_key_record(CRUX_CREDENTIAL_KEY_ED25519, record, 0x66);
    CHECK(crux_credential_write(CRUX_CREDENTIAL_RECORD_SSH_HOST_KEY, record, imported_length) ==
          CRUX_CREDENTIAL_OK);
    CHECK(credential_fake_event_count_of(CRUX_CREDENTIAL_EVENT_KEY_REPLACED) == 1u);
    CHECK(crux_credential_write(CRUX_CREDENTIAL_RECORD_TLS_CERTIFICATE, certificate,
                                sizeof(certificate)) == CRUX_CREDENTIAL_OK);
    CHECK(credential_fake_event_count_of(CRUX_CREDENTIAL_EVENT_KEY_REPLACED) == 1u);
    CHECK(crux_credential_key_public(CRUX_CREDENTIAL_RECORD_SSH_HOST_KEY, public_key,
                                     sizeof(public_key), &public_length) == CRUX_CREDENTIAL_OK);
    CHECK(public_key[0] == 0x66);

    /* The buffer rules of a read. */
    uint8_t tiny[4];
    CHECK(crux_credential_key_public(CRUX_CREDENTIAL_RECORD_SSH_HOST_KEY, tiny, sizeof(tiny),
                                     &length) == CRUX_CREDENTIAL_ERR_TOO_LARGE);
    CHECK(length == 32u);
    CHECK(crux_credential_key_public(CRUX_CREDENTIAL_RECORD_SSH_HOST_KEY, tiny, sizeof(tiny),
                                     NULL) == CRUX_CREDENTIAL_ERR_INVALID_ARGUMENT);

    /* There is nothing to rotate in a key that does not exist. */
    test_fresh_empty();
    CHECK(crux_credential_key_replace(CRUX_CREDENTIAL_RECORD_TLS_KEY,
                                      CRUX_CREDENTIAL_KEY_ECDSA_P256) ==
          CRUX_CREDENTIAL_ERR_NOT_FOUND);
    CHECK(crux_credential_key_public(CRUX_CREDENTIAL_RECORD_TLS_KEY, public_key, sizeof(public_key),
                                     &length) == CRUX_CREDENTIAL_ERR_NOT_FOUND);
    CHECK(credential_fake_keygen_count() == 0u);

    /* Only the two key records take these operations, and only with a known type. */
    CHECK(
        crux_credential_key_ensure(CRUX_CREDENTIAL_RECORD_ACCOUNTS, CRUX_CREDENTIAL_KEY_ED25519) ==
        CRUX_CREDENTIAL_ERR_INVALID_ARGUMENT);
    CHECK(crux_credential_key_ensure(CRUX_CREDENTIAL_RECORD_TLS_CERTIFICATE,
                                     CRUX_CREDENTIAL_KEY_ED25519) ==
          CRUX_CREDENTIAL_ERR_INVALID_ARGUMENT);
    // NOLINTNEXTLINE(clang-analyzer-optin.core.EnumCastOutOfRange)
    const crux_credential_key_type_t type_three = (crux_credential_key_type_t)3;
    CHECK(crux_credential_key_ensure(CRUX_CREDENTIAL_RECORD_TLS_KEY, type_three) ==
          CRUX_CREDENTIAL_ERR_INVALID_ARGUMENT);
    CHECK(
        crux_credential_key_replace(CRUX_CREDENTIAL_RECORD_ACCOUNTS, CRUX_CREDENTIAL_KEY_ED25519) ==
        CRUX_CREDENTIAL_ERR_INVALID_ARGUMENT);
    CHECK(crux_credential_key_public(CRUX_CREDENTIAL_RECORD_ACCOUNTS, public_key,
                                     sizeof(public_key),
                                     &length) == CRUX_CREDENTIAL_ERR_INVALID_ARGUMENT);
    CHECK(credential_fake_keygen_count() == 0u);

    /* A failed generation stores nothing. */
    credential_fake_keygen_fail(true);
    CHECK(crux_credential_key_ensure(CRUX_CREDENTIAL_RECORD_TLS_KEY,
                                     CRUX_CREDENTIAL_KEY_ECDSA_P256) ==
          CRUX_CREDENTIAL_ERR_ENTROPY);
    credential_fake_keygen_fail(false);
    credential_fake_keygen_bad_lengths(true);
    CHECK(crux_credential_key_ensure(CRUX_CREDENTIAL_RECORD_TLS_KEY,
                                     CRUX_CREDENTIAL_KEY_ECDSA_P256) ==
          CRUX_CREDENTIAL_ERR_ENTROPY);
    credential_fake_keygen_bad_lengths(false);
    CHECK(crux_credential_state() == CRUX_CREDENTIAL_STATE_EMPTY);
    CHECK(!test_slot_exists(TEST_SLOT_A) && !test_slot_exists(TEST_SLOT_B));
    CHECK(crux_credential_test_buffers_clear());

    /* No nonce stores nothing either, and is told apart from a storage failure. */
    credential_fake_random_fail(0);
    CHECK(crux_credential_key_ensure(CRUX_CREDENTIAL_RECORD_TLS_KEY,
                                     CRUX_CREDENTIAL_KEY_ECDSA_P256) ==
          CRUX_CREDENTIAL_ERR_ENTROPY);
    credential_fake_random_fail(0);
    CHECK(crux_credential_write(CRUX_CREDENTIAL_RECORD_ACCOUNTS, accounts_one,
                                sizeof(accounts_one)) == CRUX_CREDENTIAL_ERR_ENTROPY);
    CHECK(crux_credential_state() == CRUX_CREDENTIAL_STATE_EMPTY);
    CHECK(!test_slot_exists(TEST_SLOT_A) && !test_slot_exists(TEST_SLOT_B));

    /* A key record that only a holder of the key could have stored, which cannot be split. */
    const uint8_t junk[] = "not a key record";
    const test_record_t junk_key = {CRUX_CREDENTIAL_RECORD_TLS_KEY, junk, sizeof(junk)};
    test_fresh();
    test_image_build(image_a, 3, &junk_key, 1);
    test_slot_write(TEST_SLOT_A, image_a, sizeof(image_a));
    CHECK(test_boot() == CRUX_CREDENTIAL_OK);
    CHECK(crux_credential_key_public(CRUX_CREDENTIAL_RECORD_TLS_KEY, public_key, sizeof(public_key),
                                     &length) == CRUX_CREDENTIAL_ERR_READ);
    CHECK(length == 0);

    /* Parsing alone needs no store. */
    uint8_t good[CRUX_CREDENTIAL_KEY_RECORD_BYTES_MAX];
    size_t good_length = test_key_record(CRUX_CREDENTIAL_KEY_ED25519, good, 0x44);
    CHECK(crux_credential_key_parse(good, good_length, &view) == CRUX_CREDENTIAL_OK);
    CHECK(crux_credential_key_parse(NULL, good_length, &view) ==
          CRUX_CREDENTIAL_ERR_INVALID_ARGUMENT);
    CHECK(crux_credential_key_parse(good, good_length, NULL) ==
          CRUX_CREDENTIAL_ERR_INVALID_ARGUMENT);
    CHECK(crux_credential_key_parse(good, good_length - 1u, &view) ==
          CRUX_CREDENTIAL_ERR_INVALID_ARGUMENT);
    for (size_t index = 0; index < CRUX_CREDENTIAL_KEY_HEADER_BYTES; ++index) {
        good[index] ^= 0x40u;
        CHECK(crux_credential_key_parse(good, good_length, &view) ==
              CRUX_CREDENTIAL_ERR_INVALID_ARGUMENT);
        good[index] ^= 0x40u;
    }
}

static void test_erase(void)
{
    test_fresh_empty();
    CHECK(crux_credential_write(CRUX_CREDENTIAL_RECORD_ACCOUNTS, accounts_one,
                                sizeof(accounts_one)) == CRUX_CREDENTIAL_OK);
    credential_fake_events_clear();
    CHECK(crux_credential_erase() == CRUX_CREDENTIAL_OK);
    CHECK(credential_fake_event_count_of(CRUX_CREDENTIAL_EVENT_ERASED) == 1u);
    CHECK(crux_credential_state() == CRUX_CREDENTIAL_STATE_UNUSABLE);
    CHECK(!test_slot_exists(TEST_SLOT_A) && !test_slot_exists(TEST_SLOT_B));
    expect_refuses(CRUX_CREDENTIAL_ERR_UNUSABLE);
    /* Repeating it, on slots that are already gone, is not an error. */
    CHECK(crux_credential_erase() == CRUX_CREDENTIAL_OK);
    CHECK(test_boot() == CRUX_CREDENTIAL_OK);
    CHECK(crux_credential_state() == CRUX_CREDENTIAL_STATE_EMPTY);

    /* From EMPTY too, which still ends in UNUSABLE, because provisioning closed with it. */
    CHECK(crux_credential_erase() == CRUX_CREDENTIAL_OK);
    CHECK(crux_credential_state() == CRUX_CREDENTIAL_STATE_UNUSABLE);
}

/* The states an interrupted erase can leave, as the load table reads them. faults.c cuts one. */
static void test_interrupted_erase_states(void)
{
    static uint8_t zeros[CRUX_CREDENTIAL_SLOT_BYTES];

    /* One slot zeroed, the other not reached: the valid one still loads. */
    test_fresh();
    test_image_build(image_a, 3, &record_accounts_one, 1);
    test_slot_write(TEST_SLOT_A, zeros, sizeof(zeros));
    test_slot_write(TEST_SLOT_B, image_a, sizeof(image_a));
    CHECK(test_boot() == CRUX_CREDENTIAL_OK);
    CHECK(crux_credential_state() == CRUX_CREDENTIAL_STATE_VALID);
    CHECK(credential_fake_slot_invalid_count('a') == 1u);

    /* Both zeroed: corrupt, never a first boot. */
    test_fresh();
    test_slot_write(TEST_SLOT_A, zeros, sizeof(zeros));
    test_slot_write(TEST_SLOT_B, zeros, sizeof(zeros));
    CHECK(test_boot() == CRUX_CREDENTIAL_ERR_UNUSABLE);

    /* One removed, the other still zeros: corrupt next to absent, so the operator repeats it. */
    test_fresh();
    test_slot_write(TEST_SLOT_B, zeros, sizeof(zeros));
    CHECK(test_boot() == CRUX_CREDENTIAL_ERR_UNUSABLE);
    CHECK(crux_credential_erase() == CRUX_CREDENTIAL_OK);
    CHECK(test_boot() == CRUX_CREDENTIAL_OK);
    CHECK(crux_credential_state() == CRUX_CREDENTIAL_STATE_EMPTY);
}

static void test_info_carries_the_declaration(void)
{
    const crux_credential_store_info_t declared = {
        .basis = CRUX_CREDENTIAL_BASIS_NONE,
        .evidence = CRUX_CREDENTIAL_EVIDENCE_HOST_FAKE,
        .forgery_detected_without_device_id = true,
    };
    crux_credential_store_info_t info;
    test_fresh();
    credential_fake_declare(&declared);
    CHECK(test_boot() == CRUX_CREDENTIAL_OK);
    CHECK(crux_credential_store_get_info(NULL) == CRUX_ERR_INVALID_ARGUMENT);
    CHECK(crux_credential_store_get_info(&info) == CRUX_OK);
    CHECK(info.state == CRUX_CREDENTIAL_STATE_EMPTY);
    CHECK(info.basis == CRUX_CREDENTIAL_BASIS_NONE);
    CHECK(info.evidence == CRUX_CREDENTIAL_EVIDENCE_HOST_FAKE);
    CHECK(!info.confidential_against_medium_reader);
    CHECK(info.forgery_detected_without_device_id);
    CHECK(!info.rollback_detected);

    CHECK(crux_credential_write(CRUX_CREDENTIAL_RECORD_ACCOUNTS, accounts_one,
                                sizeof(accounts_one)) == CRUX_CREDENTIAL_OK);
    CHECK(crux_credential_store_get_info(&info) == CRUX_OK);
    CHECK(info.state == CRUX_CREDENTIAL_STATE_VALID);
    CHECK(info.forgery_detected_without_device_id);

    /* Each change of state is reported with the state it reached. */
    size_t states = 0;
    for (size_t index = 0; index < credential_fake_event_count(); ++index) {
        const crux_credential_event_t *event = credential_fake_event(index);
        if (event->type == CRUX_CREDENTIAL_EVENT_STATE) {
            CHECK(event->state ==
                  (states == 0 ? CRUX_CREDENTIAL_STATE_EMPTY : CRUX_CREDENTIAL_STATE_VALID));
            states += 1;
        }
    }
    CHECK(states == 2u);

    CHECK(crux_credential_erase() == CRUX_CREDENTIAL_OK);
    CHECK(crux_credential_store_get_info(&info) == CRUX_OK);
    CHECK(info.state == CRUX_CREDENTIAL_STATE_UNUSABLE);
    CHECK(info.forgery_detected_without_device_id);
}

/* A provider that cannot run, such as one without a device ID, must never read as a first boot. */
static void test_provider_that_cannot_run(void)
{
    test_fresh();
    credential_fake_declare(NULL);
    CHECK(test_boot() == CRUX_CREDENTIAL_ERR_UNUSABLE);
    CHECK(crux_credential_state() == CRUX_CREDENTIAL_STATE_UNUSABLE);
    expect_refuses(CRUX_CREDENTIAL_ERR_UNUSABLE);
    crux_credential_store_info_t info;
    CHECK(crux_credential_store_get_info(&info) == CRUX_OK);
    CHECK(info.state == CRUX_CREDENTIAL_STATE_UNUSABLE);
    CHECK(info.basis == CRUX_CREDENTIAL_BASIS_NONE);
    CHECK(!info.confidential_against_medium_reader);
    CHECK(!test_slot_exists(TEST_SLOT_A) && !test_slot_exists(TEST_SLOT_B));

    /* A basis that cannot be confidential cannot claim to be. */
    const crux_credential_store_info_t overclaim = {
        .basis = CRUX_CREDENTIAL_BASIS_DEVICE_ID_KEY,
        .evidence = CRUX_CREDENTIAL_EVIDENCE_SOFTWARE,
        .confidential_against_medium_reader = true,
    };
    test_fresh();
    credential_fake_declare(&overclaim);
    CHECK(test_boot() == CRUX_CREDENTIAL_ERR_UNUSABLE);
    CHECK(crux_credential_store_get_info(&info) == CRUX_OK);
    CHECK(!info.confidential_against_medium_reader);
    const crux_credential_store_info_t overclaim_none = {
        .basis = CRUX_CREDENTIAL_BASIS_NONE,
        .evidence = CRUX_CREDENTIAL_EVIDENCE_HOST_FAKE,
        .confidential_against_medium_reader = true,
    };
    test_fresh();
    credential_fake_declare(&overclaim_none);
    CHECK(test_boot() == CRUX_CREDENTIAL_ERR_UNUSABLE);
    CHECK(crux_credential_store_get_info(&info) == CRUX_OK);
    CHECK(!info.confidential_against_medium_reader);
}

/* Slots are not bound to their directory: a swap reads the same, and the next save carries on. */
static void test_swapped_slots(void)
{
    test_fresh_empty();
    CHECK(crux_credential_write(CRUX_CREDENTIAL_RECORD_ACCOUNTS, accounts_one,
                                sizeof(accounts_one)) == CRUX_CREDENTIAL_OK);
    CHECK(crux_credential_write(CRUX_CREDENTIAL_RECORD_ACCOUNTS, accounts_two,
                                sizeof(accounts_two)) == CRUX_CREDENTIAL_OK);
    test_slot_swap();
    CHECK(test_boot() == CRUX_CREDENTIAL_OK);
    CHECK(credential_fake_event_count_of(CRUX_CREDENTIAL_EVENT_SLOT_INVALID) == 0u);
    test_expect_record(CRUX_CREDENTIAL_RECORD_ACCOUNTS, accounts_two, sizeof(accounts_two));
    CHECK(crux_credential_write(CRUX_CREDENTIAL_RECORD_ACCOUNTS, accounts_one,
                                sizeof(accounts_one)) == CRUX_CREDENTIAL_OK);
    CHECK(test_boot() == CRUX_CREDENTIAL_OK);
    test_expect_record(CRUX_CREDENTIAL_RECORD_ACCOUNTS, accounts_one, sizeof(accounts_one));
}

/* A card moved to another board, which is what a wrong device key looks like. */
static void test_wrong_device_key(void)
{
    test_fresh_empty();
    CHECK(crux_credential_write(CRUX_CREDENTIAL_RECORD_ACCOUNTS, accounts_one,
                                sizeof(accounts_one)) == CRUX_CREDENTIAL_OK);
    credential_fake_device_key(0xBAD);
    credential_fake_events_clear();
    CHECK(test_boot() == CRUX_CREDENTIAL_ERR_UNUSABLE);
    CHECK(crux_credential_state() == CRUX_CREDENTIAL_STATE_UNUSABLE);
    CHECK(credential_fake_slot_invalid_count('a') == 1u);
    CHECK(credential_fake_slot_invalid_count('b') == 1u);
    expect_refuses(CRUX_CREDENTIAL_ERR_UNUSABLE);

    /* Back on its own board, nothing was lost. */
    credential_fake_device_key(0x1178u);
    CHECK(test_boot() == CRUX_CREDENTIAL_OK);
    test_expect_record(CRUX_CREDENTIAL_RECORD_ACCOUNTS, accounts_one, sizeof(accounts_one));
}

/* An image from another kind of provider, or a format this build cannot read, is never absent. */
static void test_other_format_and_protection(void)
{
    test_fresh();
    uint8_t payload[CRUX_CREDENTIAL_TABLE_HEADER_BYTES];
    size_t length = crux_credential_payload_empty(payload);
    test_image_seal(image_a, 3, CRUX_CREDENTIAL_FORMAT,
                    (uint16_t)CRUX_CREDENTIAL_BASIS_DEVICE_ID_KEY, payload, length);
    test_slot_write(TEST_SLOT_A, image_a, sizeof(image_a));
    CHECK(test_boot() == CRUX_CREDENTIAL_ERR_UNUSABLE);

    test_fresh();
    test_image_seal(image_a, 3, 0, (uint16_t)CRUX_CREDENTIAL_BASIS_NONE, payload, length);
    test_slot_write(TEST_SLOT_B, image_a, sizeof(image_a));
    CHECK(test_boot() == CRUX_CREDENTIAL_ERR_UNUSABLE);
}

/* A tag proves who wrote a payload, not that it is well formed. */
static void test_malformed_payload_behind_a_good_tag(void)
{
    static const struct {
        uint8_t bytes[28];
        size_t length;
    } cases[] = {
        {{0}, 0},                                   /* No table at all. */
        {{0, 0}, 2},                                /* A partial table header. */
        {{5, 0, 0, 0}, 4},                          /* More entries than kinds of record. */
        {{1, 0, 0, 0}, 4},                          /* An entry that is not there. */
        {{0, 0, 1, 0}, 4},                          /* A reserved field that is not zero. */
        {{1, 0, 0, 0, 9, 0, 0, 0, 0, 0, 0, 0}, 12}, /* An unknown record. */
        {{1, 0, 0, 0, 1, 0, 1, 0, 0, 0, 0, 0}, 12}, /* A reserved entry field. */
        {{1, 0, 0, 0, 1, 0, 0, 0, 9, 0, 0, 0}, 12}, /* A record past the end. */
        {{0, 0, 0, 0, 0x7F}, 5},                    /* Bytes that no entry claims. */
        {{1, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0}, 12}, /* A record with no bytes. */
        {{2, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0, 0xAA, 0xBB},
         22}, /* A repeat. */
    };
    for (size_t index = 0; index < sizeof(cases) / sizeof(cases[0]); ++index) {
        test_fresh();
        (void)snprintf(credential_test_context, sizeof(credential_test_context), "payload case %zu",
                       index);
        test_image_seal(image_a, 3, CRUX_CREDENTIAL_FORMAT, (uint16_t)CRUX_CREDENTIAL_BASIS_NONE,
                        cases[index].bytes, cases[index].length);
        test_slot_write(TEST_SLOT_A, image_a, sizeof(image_a));
        CHECK(test_boot() == CRUX_CREDENTIAL_ERR_UNUSABLE);
        CHECK(credential_fake_slot_invalid_count('a') == 1u);
    }
    credential_test_context[0] = '\0';

    /* A header that declares an end past the slot is corrupt, whatever the tag says. */
    test_fresh();
    test_image_build(image_a, 3, &record_accounts_one, 1);
    test_slot_write(TEST_SLOT_A, image_a, sizeof(image_a));
    test_slot_flip(TEST_SLOT_A, 18, 0x01);
    CHECK(test_boot() == CRUX_CREDENTIAL_ERR_UNUSABLE);

    /* A file cut short of its declared end is corrupt, because the header promised more. */
    test_fresh();
    test_image_build(image_a, 3, &record_accounts_one, 1);
    test_slot_write(TEST_SLOT_A, image_a, CRUX_CREDENTIAL_HEADER_BYTES + 4u);
    CHECK(test_boot() == CRUX_CREDENTIAL_ERR_UNUSABLE);
}

/* A build that changed the slot size finds files of another size. */
static void test_slot_size_changes(void)
{
    /* Padding is outside the tag, so an image in a smaller file is valid. The build raised the
     * size: saves replace each small file in turn with a full one. */
    const size_t end = CRUX_CREDENTIAL_HEADER_BYTES + CRUX_CREDENTIAL_TABLE_HEADER_BYTES +
                       CRUX_CREDENTIAL_TABLE_ENTRY_BYTES + sizeof(accounts_one) +
                       CRUX_CREDENTIAL_TAG_BYTES;
    test_fresh();
    test_image_build(image_a, 3, &record_accounts_one, 1);
    test_slot_write(TEST_SLOT_A, image_a, end);
    test_image_build(image_a, 2, &record_accounts_one, 1);
    test_slot_write(TEST_SLOT_B, image_a, end);
    CHECK(test_boot() == CRUX_CREDENTIAL_OK);
    test_expect_record(CRUX_CREDENTIAL_RECORD_ACCOUNTS, accounts_one, sizeof(accounts_one));
    CHECK(crux_credential_write(CRUX_CREDENTIAL_RECORD_ACCOUNTS, accounts_two,
                                sizeof(accounts_two)) == CRUX_CREDENTIAL_OK);
    CHECK(test_slot_read(TEST_SLOT_B, large, sizeof(large)) == CRUX_CREDENTIAL_SLOT_BYTES);
    CHECK(test_boot() == CRUX_CREDENTIAL_OK);
    test_expect_record(CRUX_CREDENTIAL_RECORD_ACCOUNTS, accounts_two, sizeof(accounts_two));
    CHECK(crux_credential_write(CRUX_CREDENTIAL_RECORD_ACCOUNTS, accounts_one,
                                sizeof(accounts_one)) == CRUX_CREDENTIAL_OK);
    CHECK(test_slot_read(TEST_SLOT_A, large, sizeof(large)) == CRUX_CREDENTIAL_SLOT_BYTES);

    /* The build lowered it: larger files keep working, in place, and their extra bytes are ignored.
     */
    static uint8_t oversize[CRUX_CREDENTIAL_SLOT_BYTES + 512u];
    test_fresh();
    test_image_build(oversize, 3, &record_accounts_one, 1);
    memset(oversize + CRUX_CREDENTIAL_SLOT_BYTES, 0x77, 512u);
    test_slot_write(TEST_SLOT_A, oversize, sizeof(oversize));
    test_slot_write(TEST_SLOT_B, oversize, sizeof(oversize));
    CHECK(test_boot() == CRUX_CREDENTIAL_OK);
    CHECK(crux_credential_write(CRUX_CREDENTIAL_RECORD_ACCOUNTS, accounts_two,
                                sizeof(accounts_two)) == CRUX_CREDENTIAL_OK);
    CHECK(test_boot() == CRUX_CREDENTIAL_OK);
    test_expect_record(CRUX_CREDENTIAL_RECORD_ACCOUNTS, accounts_two, sizeof(accounts_two));
    CHECK(crux_credential_erase() == CRUX_CREDENTIAL_OK);
    CHECK(!test_slot_exists(TEST_SLOT_A) && !test_slot_exists(TEST_SLOT_B));
}

/* The limits the owner accepted, shown so that a change in them cannot pass unnoticed. */
static void test_accepted_limits(void)
{
    uint8_t key[CRUX_CREDENTIAL_KEY_RECORD_BYTES_MAX];
    size_t key_length = test_key_record(CRUX_CREDENTIAL_KEY_ED25519, key, 0x55);

    /* S6: losing both directory entries reads as a first boot. The tag protects contents only. */
    test_fresh_empty();
    CHECK(crux_credential_write(CRUX_CREDENTIAL_RECORD_ACCOUNTS, accounts_one,
                                sizeof(accounts_one)) == CRUX_CREDENTIAL_OK);
    test_slot_remove(TEST_SLOT_A);
    test_slot_remove(TEST_SLOT_B);
    CHECK(test_boot() == CRUX_CREDENTIAL_OK);
    CHECK(crux_credential_state() == CRUX_CREDENTIAL_STATE_EMPTY);
    expect_absent(CRUX_CREDENTIAL_RECORD_ACCOUNTS);

    /* Losing only one entry does not: the other slot still loads, and no event says it went. */
    test_fresh_empty();
    CHECK(crux_credential_write(CRUX_CREDENTIAL_RECORD_ACCOUNTS, accounts_one,
                                sizeof(accounts_one)) == CRUX_CREDENTIAL_OK);
    test_slot_remove(TEST_SLOT_A);
    CHECK(test_boot() == CRUX_CREDENTIAL_OK);
    CHECK(crux_credential_state() == CRUX_CREDENTIAL_STATE_VALID);
    test_expect_record(CRUX_CREDENTIAL_RECORD_ACCOUNTS, accounts_one, sizeof(accounts_one));

    /* Rollback is not detected: both slots restored from an old copy read as current. */
    test_fresh_empty();
    CHECK(crux_credential_write(CRUX_CREDENTIAL_RECORD_ACCOUNTS, accounts_one,
                                sizeof(accounts_one)) == CRUX_CREDENTIAL_OK);
    CHECK(test_slot_read(TEST_SLOT_A, image_a, sizeof(image_a)) == CRUX_CREDENTIAL_SLOT_BYTES);
    CHECK(test_slot_read(TEST_SLOT_B, image_b, sizeof(image_b)) == CRUX_CREDENTIAL_SLOT_BYTES);
    CHECK(crux_credential_write(CRUX_CREDENTIAL_RECORD_ACCOUNTS, accounts_two,
                                sizeof(accounts_two)) == CRUX_CREDENTIAL_OK);
    CHECK(crux_credential_write(CRUX_CREDENTIAL_RECORD_ACCOUNTS, accounts_two,
                                sizeof(accounts_two)) == CRUX_CREDENTIAL_OK);
    test_slot_write(TEST_SLOT_A, image_a, sizeof(image_a));
    test_slot_write(TEST_SLOT_B, image_b, sizeof(image_b));
    credential_fake_events_clear();
    CHECK(test_boot() == CRUX_CREDENTIAL_OK);
    test_expect_record(CRUX_CREDENTIAL_RECORD_ACCOUNTS, accounts_one, sizeof(accounts_one));
    CHECK(credential_fake_event_count_of(CRUX_CREDENTIAL_EVENT_SLOT_INVALID) == 0u);
    crux_credential_store_info_t info;
    CHECK(crux_credential_store_get_info(&info) == CRUX_OK);
    CHECK(!info.rollback_detected);

    /* Restoring an old copy can bring a record's absence back. */
    test_fresh_empty();
    CHECK(crux_credential_write(CRUX_CREDENTIAL_RECORD_ACCOUNTS, accounts_one,
                                sizeof(accounts_one)) == CRUX_CREDENTIAL_OK);
    CHECK(test_slot_read(TEST_SLOT_A, image_a, sizeof(image_a)) == CRUX_CREDENTIAL_SLOT_BYTES);
    CHECK(crux_credential_write(CRUX_CREDENTIAL_RECORD_SSH_HOST_KEY, key, key_length) ==
          CRUX_CREDENTIAL_OK);
    test_slot_write(TEST_SLOT_A, image_a, sizeof(image_a));
    test_slot_write(TEST_SLOT_B, image_a, sizeof(image_a));
    CHECK(test_boot() == CRUX_CREDENTIAL_OK);
    expect_absent(CRUX_CREDENTIAL_RECORD_SSH_HOST_KEY);

    /* A corrupt newest slot falls back to the older generation, with an event, and the next save
     * makes that older content permanent. */
    test_fresh_empty();
    CHECK(crux_credential_write(CRUX_CREDENTIAL_RECORD_ACCOUNTS, accounts_one,
                                sizeof(accounts_one)) == CRUX_CREDENTIAL_OK);
    CHECK(crux_credential_write(CRUX_CREDENTIAL_RECORD_ACCOUNTS, accounts_two,
                                sizeof(accounts_two)) == CRUX_CREDENTIAL_OK);
    test_slot_flip(TEST_SLOT_B, CRUX_CREDENTIAL_HEADER_BYTES + 1u, 0x01);
    credential_fake_events_clear();
    CHECK(test_boot() == CRUX_CREDENTIAL_OK);
    CHECK(credential_fake_slot_invalid_count('b') == 1u);
    test_expect_record(CRUX_CREDENTIAL_RECORD_ACCOUNTS, accounts_one, sizeof(accounts_one));
    CHECK(crux_credential_write(CRUX_CREDENTIAL_RECORD_TLS_KEY, key, key_length) ==
          CRUX_CREDENTIAL_OK);
    CHECK(test_boot() == CRUX_CREDENTIAL_OK);
    test_expect_record(CRUX_CREDENTIAL_RECORD_ACCOUNTS, accounts_one, sizeof(accounts_one));
}

void credential_conformance_run(void)
{
    test_before_start();
    test_start_arguments();
    test_load_table();
    test_unreadable_root();
    test_records_read_write();
    test_limits();
    test_generation_order_and_ties();
    test_new_record_reaches_both_slots();
    test_replacing_writes_one_slot();
    test_repair_at_start();
    test_unusable_refuses_all_but_erase();
    test_keys();
    test_erase();
    test_interrupted_erase_states();
    test_info_carries_the_declaration();
    test_provider_that_cannot_run();
    test_swapped_slots();
    test_wrong_device_key();
    test_other_format_and_protection();
    test_malformed_payload_behind_a_good_tag();
    test_slot_size_changes();
    test_accepted_limits();
}
