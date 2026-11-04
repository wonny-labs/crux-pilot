#include "conformance.h"
#include "credential_test.h"
#include "file_native.h"
#include <ff.h>
#include <zephyr/fs/fs.h>
#include <zephyr/ztest.h>

/* The store's contract on real Zephyr FatFs over a RAM disk. This is not a card and shows no power
 * loss, and the protection is the test-only fake, because PSA is not qualified (Issue #142). */
static FATFS fat;
static struct fs_mount_t mount = {
    .type = FS_FATFS,
    .mnt_point = "/RAM:",
    .fs_data = &fat,
    .flags = FS_MOUNT_FLAG_NO_FORMAT,
};

void credential_medium_blank(void)
{
    test_slot_remove(TEST_SLOT_A);
    test_slot_remove(TEST_SLOT_B);
}

static void *setup(void)
{
    zassert_ok(fs_mkfs(FS_FATFS, (uintptr_t) "RAM:", NULL, 0));
    zassert_ok(fs_mount(&mount));
    zassert_ok(fs_mkdir("/RAM:/secure-a"));
    zassert_ok(fs_mkdir("/RAM:/secure-b"));
    zassert_equal(crux_file_zephyr_bind_root(TEST_ROOT_A, "/RAM:/secure-a"), CRUX_FILE_OK);
    zassert_equal(crux_file_zephyr_bind_root(TEST_ROOT_B, "/RAM:/secure-b"), CRUX_FILE_OK);
    return NULL;
}

ZTEST(credential_fatfs, test_conformance_on_fatfs)
{
    credential_conformance_run();
    zassert_equal(credential_test_failures, 0, "%u of %u checks failed", credential_test_failures,
                  credential_test_checks);
}

/* Both slots reach full size and a save allocates nothing, so saves overwrite one file's clusters.
 */
ZTEST(credential_fatfs, test_saves_keep_the_slot_size_across_a_remount)
{
    static const uint8_t first[] = "first value";
    static const uint8_t second[] = "second value, a little longer";
    crux_file_info_t info;

    test_fresh_empty();
    zassert_equal(crux_credential_write(CRUX_CREDENTIAL_RECORD_ACCOUNTS, first, sizeof(first)),
                  CRUX_CREDENTIAL_OK);
    zassert_equal(crux_credential_write(CRUX_CREDENTIAL_RECORD_ACCOUNTS, second, sizeof(second)),
                  CRUX_CREDENTIAL_OK);
    zassert_equal(crux_credential_key_ensure(CRUX_CREDENTIAL_RECORD_SSH_HOST_KEY,
                                             CRUX_CREDENTIAL_KEY_ED25519),
                  CRUX_CREDENTIAL_OK);

    /* The same in-process RAM disk, so this is not persistence evidence. */
    zassert_ok(fs_unmount(&mount));
    zassert_ok(fs_mount(&mount));
    zassert_equal(test_boot(), CRUX_CREDENTIAL_OK);
    zassert_equal(crux_credential_state(), CRUX_CREDENTIAL_STATE_VALID);
    test_expect_record(CRUX_CREDENTIAL_RECORD_ACCOUNTS, second, sizeof(second));
    zassert_equal(credential_test_failures, 0);

    for (crux_file_root_t root = TEST_ROOT_A; root <= TEST_ROOT_B; ++root) {
        zassert_equal(crux_file_stat(root, TEST_SLOT_NAME, &info), CRUX_FILE_OK);
        zassert_equal(info.size, CRUX_CREDENTIAL_SLOT_BYTES);
    }
    zassert_equal(crux_credential_erase(), CRUX_CREDENTIAL_OK);
    zassert_equal(crux_file_stat(TEST_ROOT_A, TEST_SLOT_NAME, &info), CRUX_FILE_ERR_NOT_FOUND);
    zassert_equal(crux_file_stat(TEST_ROOT_B, TEST_SLOT_NAME, &info), CRUX_FILE_ERR_NOT_FOUND);
}

ZTEST_SUITE(credential_fatfs, NULL, setup, NULL, NULL, NULL);
