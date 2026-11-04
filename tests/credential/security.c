/*
 * What the store does with secrets and with slots it did not write, on the File fake. The fake's
 * cipher is not encryption. These cases show the store's handling only: confidentiality and forgery
 * resistance belong to the real provider, under Issue #142.
 */
#include "credential_test.h"
#include "fake_file.h"

static uint8_t image_a[CRUX_CREDENTIAL_SLOT_BYTES];
static uint8_t image_b[CRUX_CREDENTIAL_SLOT_BYTES];

static const uint8_t secret[] = "a-secret-value-that-must-stay-sealed";

#define ACCOUNTS CRUX_CREDENTIAL_RECORD_ACCOUNTS
#define HOST_KEY CRUX_CREDENTIAL_RECORD_SSH_HOST_KEY

static bool holds(const uint8_t *haystack, size_t length, const uint8_t *needle,
                  size_t needle_length)
{
    for (size_t index = 0; index + needle_length <= length; ++index) {
        if (memcmp(haystack + index, needle, needle_length) == 0) {
            return true;
        }
    }
    return false;
}

/* The medium holds no record in the clear, whichever path wrote it. */
static void test_nothing_plain_on_the_medium(void)
{
    uint8_t key[CRUX_CREDENTIAL_KEY_RECORD_BYTES_MAX];
    size_t key_length = 0;
    crux_credential_key_view_t view;

    test_fresh_empty();
    CHECK(crux_credential_write(ACCOUNTS, secret, sizeof(secret)) == CRUX_CREDENTIAL_OK);
    CHECK(crux_credential_key_ensure(HOST_KEY, CRUX_CREDENTIAL_KEY_ED25519) == CRUX_CREDENTIAL_OK);
    CHECK(crux_credential_read(HOST_KEY, key, sizeof(key), &key_length) == CRUX_CREDENTIAL_OK);
    CHECK(crux_credential_key_parse(key, key_length, &view) == CRUX_CREDENTIAL_OK);
    CHECK(test_boot() == CRUX_CREDENTIAL_OK);
    for (size_t slot = TEST_SLOT_A; slot <= TEST_SLOT_B; ++slot) {
        CHECK(test_slot_read(slot, image_a, sizeof(image_a)) == CRUX_CREDENTIAL_SLOT_BYTES);
        CHECK(!holds(image_a, sizeof(image_a), secret, sizeof(secret)));
        CHECK(!holds(image_a, sizeof(image_a), view.private_key, view.private_length));
    }
}

/* Events hold identifiers only, and neither a value nor a key can reach one. */
static void test_no_secret_in_events(void)
{
    uint8_t key[CRUX_CREDENTIAL_KEY_RECORD_BYTES_MAX];
    size_t key_length = 0;
    crux_credential_key_view_t view;

    test_fresh_empty();
    CHECK(crux_credential_write(ACCOUNTS, secret, sizeof(secret)) == CRUX_CREDENTIAL_OK);
    CHECK(crux_credential_key_ensure(HOST_KEY, CRUX_CREDENTIAL_KEY_ED25519) == CRUX_CREDENTIAL_OK);
    CHECK(crux_credential_key_replace(HOST_KEY, CRUX_CREDENTIAL_KEY_ED25519) == CRUX_CREDENTIAL_OK);
    CHECK(crux_credential_read(HOST_KEY, key, sizeof(key), &key_length) == CRUX_CREDENTIAL_OK);
    CHECK(crux_credential_key_parse(key, key_length, &view) == CRUX_CREDENTIAL_OK);
    /* Failures, which are the paths most likely to want to say more. */
    crux_file_fake_fail(CRUX_FILE_FAKE_SYNC, 0, CRUX_FILE_ERR_IO);
    CHECK(crux_credential_write(ACCOUNTS, secret, sizeof(secret)) == CRUX_CREDENTIAL_ERR_STORE);
    test_slot_flip(TEST_SLOT_A, CRUX_CREDENTIAL_HEADER_BYTES, 0x01);
    CHECK(test_boot() == CRUX_CREDENTIAL_OK);
    CHECK(crux_credential_erase() == CRUX_CREDENTIAL_OK);

    CHECK(credential_fake_event_count() > 0u);
    for (size_t index = 0; index < credential_fake_event_count(); ++index) {
        const crux_credential_event_t *event = credential_fake_event(index);
        const uint8_t *bytes = (const uint8_t *)event;
        CHECK(!holds(bytes, sizeof(*event), secret, 8u));
        CHECK(!holds(bytes, sizeof(*event), view.private_key, 8u));
        CHECK(event->record == 0 || crux_credential_record_known((uint32_t)event->record));
    }
    size_t generated = credential_fake_event_count_of(CRUX_CREDENTIAL_EVENT_KEY_GENERATED);
    CHECK(generated == 1u);
    for (size_t index = 0; index < credential_fake_event_count(); ++index) {
        const crux_credential_event_t *event = credential_fake_event(index);
        if (event->type == CRUX_CREDENTIAL_EVENT_KEY_GENERATED ||
            event->type == CRUX_CREDENTIAL_EVENT_KEY_REPLACED) {
            CHECK(event->record == HOST_KEY);
        }
    }
}

/* A key that failed to save produces no generated event, because it does not yet exist. */
static void test_key_event_follows_the_save(void)
{
    test_fresh_empty();
    crux_file_fake_fail(CRUX_FILE_FAKE_WRITE, 0, CRUX_FILE_ERR_IO);
    CHECK(crux_credential_key_ensure(HOST_KEY, CRUX_CREDENTIAL_KEY_ED25519) ==
          CRUX_CREDENTIAL_ERR_STORE);
    CHECK(credential_fake_event_count_of(CRUX_CREDENTIAL_EVENT_KEY_GENERATED) == 0u);
    CHECK(credential_fake_event_count_of(CRUX_CREDENTIAL_EVENT_SAVE_FAILED) == 1u);
    uint8_t public_key[CRUX_CREDENTIAL_KEY_PUBLIC_BYTES_MAX];
    size_t length = 0;
    CHECK(crux_credential_key_public(HOST_KEY, public_key, sizeof(public_key), &length) ==
          CRUX_CREDENTIAL_ERR_NOT_FOUND);
    /* The next attempt generates again, which is fine: that key was never used. */
    CHECK(crux_credential_key_ensure(HOST_KEY, CRUX_CREDENTIAL_KEY_ED25519) == CRUX_CREDENTIAL_OK);
    CHECK(credential_fake_keygen_count() == 2u);
}

/* A slot written without the key fails its tag and is not used. */
static void test_forged_slot(void)
{
    const test_record_t honest = {ACCOUNTS, secret, sizeof(secret)};
    const uint8_t forged_value[] = "forged accounts";
    const test_record_t forged = {ACCOUNTS, forged_value, sizeof(forged_value)};

    /* With a higher generation, next to a good slot: the good slot is used. */
    test_fresh();
    test_image_build(image_a, 3, &honest, 1);
    test_slot_write(TEST_SLOT_A, image_a, sizeof(image_a));
    credential_fake_device_key(0xBAD);
    test_image_build(image_b, 9, &forged, 1);
    credential_fake_device_key(0x1178u);
    test_slot_write(TEST_SLOT_B, image_b, sizeof(image_b));
    CHECK(test_boot() == CRUX_CREDENTIAL_OK);
    CHECK(crux_credential_state() == CRUX_CREDENTIAL_STATE_VALID);
    CHECK(credential_fake_slot_invalid_count('b') == 1u);
    test_expect_record(ACCOUNTS, secret, sizeof(secret));

    /* A generation changed in the header alone, which the tag covers. */
    test_fresh();
    test_image_build(image_a, 3, &honest, 1);
    test_slot_write(TEST_SLOT_A, image_a, sizeof(image_a));
    test_image_build(image_b, 2, &forged, 1);
    image_b[15] ^= 0x40u;
    test_slot_write(TEST_SLOT_B, image_b, sizeof(image_b));
    CHECK(test_boot() == CRUX_CREDENTIAL_OK);
    CHECK(credential_fake_slot_invalid_count('b') == 1u);
    test_expect_record(ACCOUNTS, secret, sizeof(secret));

    /* A nonce changed in the header, which AES-GCM also binds, and the fake's tag covers. */
    test_fresh();
    test_image_build(image_a, 3, &honest, 1);
    image_a[25] ^= 0x01u;
    test_slot_write(TEST_SLOT_A, image_a, sizeof(image_a));
    CHECK(test_boot() == CRUX_CREDENTIAL_ERR_UNUSABLE);
}

/* The same generation twice with different contents: the tie rule decides, the same way each time.
 */
static void test_reused_generation(void)
{
    const uint8_t first[] = "first image";
    const uint8_t second[] = "second image";
    const test_record_t one = {ACCOUNTS, first, sizeof(first)};
    const test_record_t two = {ACCOUNTS, second, sizeof(second)};
    test_fresh();
    test_image_build(image_a, 4, &one, 1);
    test_image_build(image_b, 4, &two, 1);
    test_slot_write(TEST_SLOT_A, image_a, sizeof(image_a));
    test_slot_write(TEST_SLOT_B, image_b, sizeof(image_b));
    for (int boot = 0; boot < 2; ++boot) {
        CHECK(test_boot() == CRUX_CREDENTIAL_OK);
        test_expect_record(ACCOUNTS, first, sizeof(first));
    }
}

/* A bad tag in both is UNUSABLE and never EMPTY, and in one uses the other. */
static void test_bad_tags(void)
{
    const test_record_t honest = {ACCOUNTS, secret, sizeof(secret)};
    const size_t tag_offset = CRUX_CREDENTIAL_HEADER_BYTES + CRUX_CREDENTIAL_TABLE_HEADER_BYTES +
                              CRUX_CREDENTIAL_TABLE_ENTRY_BYTES + sizeof(secret);

    test_fresh();
    test_image_build(image_a, 3, &honest, 1);
    test_image_build(image_b, 4, &honest, 1);
    test_slot_write(TEST_SLOT_A, image_a, sizeof(image_a));
    test_slot_write(TEST_SLOT_B, image_b, sizeof(image_b));
    test_slot_flip(TEST_SLOT_B, tag_offset, 0x01);
    CHECK(test_boot() == CRUX_CREDENTIAL_OK);
    CHECK(crux_credential_state() == CRUX_CREDENTIAL_STATE_VALID);
    CHECK(credential_fake_slot_invalid_count('b') == 1u);
    test_expect_record(ACCOUNTS, secret, sizeof(secret));

    test_slot_flip(TEST_SLOT_A, tag_offset, 0x01);
    credential_fake_events_clear();
    CHECK(test_boot() == CRUX_CREDENTIAL_ERR_UNUSABLE);
    CHECK(crux_credential_state() == CRUX_CREDENTIAL_STATE_UNUSABLE);
    crux_credential_store_info_t info;
    CHECK(crux_credential_store_get_info(&info) == CRUX_OK);
    CHECK(info.state == CRUX_CREDENTIAL_STATE_UNUSABLE);
    CHECK(credential_fake_slot_invalid_count('a') == 1u);
    CHECK(credential_fake_slot_invalid_count('b') == 1u);
}

/* Each seal draws a fresh nonce, including the second write of a save and the retry after a
 * failure. */
static void test_fresh_nonce_per_seal(void)
{
    test_fresh_empty();
    CHECK(crux_credential_write(ACCOUNTS, secret, sizeof(secret)) == CRUX_CREDENTIAL_OK);
    CHECK(crux_credential_write(ACCOUNTS, secret, sizeof(secret)) == CRUX_CREDENTIAL_OK);
    CHECK(crux_credential_key_ensure(HOST_KEY, CRUX_CREDENTIAL_KEY_ED25519) == CRUX_CREDENTIAL_OK);
    crux_file_fake_fail(CRUX_FILE_FAKE_SYNC, 0, CRUX_FILE_ERR_IO);
    CHECK(crux_credential_write(ACCOUNTS, secret, sizeof(secret)) == CRUX_CREDENTIAL_ERR_STORE);
    CHECK(crux_credential_write(ACCOUNTS, secret, sizeof(secret)) == CRUX_CREDENTIAL_OK);
    size_t count = credential_fake_nonce_count();
    /* One first save, one replacement, an addition's two, a failure and its retry. */
    CHECK(count == 6u);
    for (size_t left = 0; left < count; ++left) {
        for (size_t right = left + 1u; right < count; ++right) {
            CHECK(memcmp(credential_fake_nonce(left), credential_fake_nonce(right),
                         CRUX_CREDENTIAL_NONCE_BYTES) != 0);
        }
    }
}

int main(void)
{
    test_nothing_plain_on_the_medium();
    test_no_secret_in_events();
    test_key_event_follows_the_save();
    test_forged_slot();
    test_reused_generation();
    test_bad_tags();
    test_fresh_nonce_per_seal();
    return credential_test_finish("credential security");
}
