/*
 * The host key's way from the credential store, on the store's test composition. Its key generation
 * and protection are fakes, so this shows the mapping and the refusal, and no real key.
 */
#include "credential_test.h"
#include "crux_ssh_access.h"
#include "ssh_check.h"

static void start_blank_store(void)
{
    crux_credential_test_reset();
    credential_fake_reset();
    credential_medium_blank();
    SSH_CHECK(crux_credential_start(TEST_ROOT_A, TEST_ROOT_B) == CRUX_CREDENTIAL_OK);
}

static void test_first_use_generates_and_later_uses_do_not(void)
{
    crux_ssh_host_key_t first;
    crux_ssh_host_key_t second;

    start_blank_store();
    SSH_CHECK(crux_ssh_host_key_load(CRUX_SSH_KEY_ED25519, &first) == CRUX_OK);
    SSH_CHECK(first.type == CRUX_SSH_KEY_ED25519);
    SSH_CHECK(first.length == 64u);
    SSH_CHECK(credential_fake_keygen_count() == 1u);

    SSH_CHECK(crux_ssh_host_key_load(CRUX_SSH_KEY_ED25519, &second) == CRUX_OK);
    SSH_CHECK(credential_fake_keygen_count() == 1u);
    SSH_CHECK(second.length == first.length);
    SSH_CHECK(memcmp(second.key, first.key, first.length) == 0);
}

static void test_private_part_comes_first(void)
{
    uint8_t record[CRUX_CREDENTIAL_KEY_RECORD_BYTES_MAX];
    crux_credential_key_view_t view;
    crux_ssh_host_key_t key;
    size_t length = 0;

    start_blank_store();
    SSH_CHECK(crux_ssh_host_key_load(CRUX_SSH_KEY_ECDSA_P256, &key) == CRUX_OK);
    SSH_CHECK(key.type == CRUX_SSH_KEY_ECDSA_P256);
    SSH_CHECK(key.length == 97u);
    SSH_CHECK(crux_credential_read(CRUX_CREDENTIAL_RECORD_SSH_HOST_KEY, record, sizeof(record),
                                   &length) == CRUX_CREDENTIAL_OK);
    SSH_CHECK(crux_credential_key_parse(record, length, &view) == CRUX_CREDENTIAL_OK);
    SSH_CHECK(memcmp(key.key, view.private_key, 32) == 0);
    SSH_CHECK(memcmp(key.key + 32, view.public_key, 65) == 0);
}

static void test_existing_key_keeps_its_type(void)
{
    crux_ssh_host_key_t key;

    start_blank_store();
    SSH_CHECK(crux_ssh_host_key_load(CRUX_SSH_KEY_ECDSA_P256, &key) == CRUX_OK);
    SSH_CHECK(crux_ssh_host_key_load(CRUX_SSH_KEY_ED25519, &key) == CRUX_OK);
    SSH_CHECK(key.type == CRUX_SSH_KEY_ECDSA_P256);
    SSH_CHECK(key.length == 97u);
    SSH_CHECK(credential_fake_keygen_count() == 1u);
}

static void test_unavailable_store_gives_no_key(void)
{
    crux_ssh_host_key_t key;

    /* Not started: the server must not start, and nothing is generated in its place. */
    crux_credential_test_reset();
    credential_fake_reset();
    credential_medium_blank();
    memset(&key, 0x5a, sizeof(key));
    SSH_CHECK(crux_ssh_host_key_load(CRUX_SSH_KEY_ED25519, &key) == CRUX_ERR_NOT_READY);
    SSH_CHECK(key.length == 0u);
    SSH_CHECK(key.key[0] == 0u);
    SSH_CHECK(credential_fake_keygen_count() == 0u);

    /* Generation fails. */
    start_blank_store();
    credential_fake_keygen_fail(true);
    SSH_CHECK(crux_ssh_host_key_load(CRUX_SSH_KEY_ED25519, &key) == CRUX_ERR_NOT_READY);
}

static void test_arguments_and_clear(void)
{
    crux_ssh_host_key_t key;

    start_blank_store();
    SSH_CHECK(crux_ssh_host_key_load(CRUX_SSH_KEY_ED25519, NULL) == CRUX_ERR_INVALID_ARGUMENT);
    // NOLINTNEXTLINE(clang-analyzer-optin.core.EnumCastOutOfRange)
    SSH_CHECK(crux_ssh_host_key_load((crux_ssh_key_type_t)99, &key) == CRUX_ERR_INVALID_ARGUMENT);
    SSH_CHECK(crux_ssh_host_key_load(CRUX_SSH_KEY_ED25519, &key) == CRUX_OK);
    crux_ssh_host_key_clear(&key);
    SSH_CHECK(key.length == 0u && key.key[0] == 0u && key.key[63] == 0u);
    crux_ssh_host_key_clear(NULL);
}

int main(void)
{
    test_first_use_generates_and_later_uses_do_not();
    test_private_part_comes_first();
    test_existing_key_keeps_its_type();
    test_unavailable_store_gives_no_key();
    test_arguments_and_clear();
    printf("%u failures\n", ssh_check_failures);
    return ssh_check_failures == 0 ? 0 : 1;
}
