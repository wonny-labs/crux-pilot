/*
 * ADR-0004 phase 0. Access, the credential store and the security events come from the Zephyr
 * module under CONFIG_CRUX_ACCESS, on real PSA providers, a real FatFs on a simulated disk and the
 * Crux Logging provider. This file is the application: it starts them in the order of
 * docs/management-in-a-product.md and checks that they work together. Test only: the entropy is
 * test-only and the first account is a declared input.
 */
#include "crux_tf_psa_threading.h"

#include <crux/access.h>
#include <crux/credential_store.h>
#include <crux/file_zephyr.h>
#include <crux/security_event.h>
#include <crux/time.h>
#include <crux_access_adapter.h>
#include <crux_credential_adapter.h>

#include <ff.h>
#include <string.h>
#include <zephyr/fs/fs.h>
#include <zephyr/ztest.h>

enum { ROOT_SLOT_A = 1, ROOT_SLOT_B = 2 };
#define MOUNT_POINT "/SD:"
#define DISK_NAME "SD:"
#define PATH_SLOT_A MOUNT_POINT "/secure-a"
#define PATH_SLOT_B MOUNT_POINT "/secure-b"
#define ADMIN "admin"
#define ADMIN_PASSWORD "module test password"
#define WRONG_PASSWORD "this is the wrong password"
#define TEST_PERMISSION "module.test"

static const char *const admin_permissions[] = {
    TEST_PERMISSION, CRUX_SECURITY_EVENT_PERMISSION_READ, CRUX_ACCESS_PERMISSION_MANAGE};
static const char *const guest_permissions[] = {"guest.only"};
static const crux_access_role_t roles[] = {
    {"guest", guest_permissions, ARRAY_SIZE(guest_permissions)},
    {"administrator", admin_permissions, ARRAY_SIZE(admin_permissions)},
};
static const crux_access_policy_t policy = {roles, ARRAY_SIZE(roles), 15u * 60u, 8u * 3600u, NULL};

static FATFS fat;
static struct fs_mount_t mount = {
    .type = FS_FATFS,
    .mnt_point = MOUNT_POINT,
    .fs_data = &fat,
    .flags = FS_MOUNT_FLAG_NO_FORMAT,
};
static crux_credential_state_t state_at_start = CRUX_CREDENTIAL_STATE_UNUSABLE;
static crux_access_session_t admin;

static bool directory_ready(const char *path)
{
    int result = fs_mkdir(path);
    return result == 0 || result == -EEXIST;
}

static bool medium_ready(void)
{
    if (fs_mount(&mount) != 0) {
        if (fs_mkfs(FS_FATFS, (uintptr_t)DISK_NAME, NULL, 0) != 0 || fs_mount(&mount) != 0) {
            return false;
        }
    }
    return directory_ready(PATH_SLOT_A) && directory_ready(PATH_SLOT_B) &&
           crux_file_zephyr_bind_root(ROOT_SLOT_A, PATH_SLOT_A) == CRUX_FILE_OK &&
           crux_file_zephyr_bind_root(ROOT_SLOT_B, PATH_SLOT_B) == CRUX_FILE_OK;
}

static size_t count_events(crux_access_session_t session, crux_security_event_id_t id)
{
    crux_security_event_record_t record;
    uint32_t cursor = 0;
    size_t count = 0;
    while (crux_security_event_read(session, &cursor, &record) == CRUX_SECURITY_READ_OK) {
        count += record.id == id;
    }
    return count;
}

static void *start_services(void)
{
    /* First, so that everything that follows can emit. */
    zassert_equal(crux_security_event_init(NULL, 0), CRUX_OK);
    zassert_equal(crux_realtime_init(), CRUX_OK);
    zassert_true(medium_ready());
    crux_credential_result_t store = crux_credential_start(ROOT_SLOT_A, ROOT_SLOT_B);
    zassert_true(store == CRUX_CREDENTIAL_OK || store == CRUX_CREDENTIAL_ERR_UNUSABLE);
    state_at_start = crux_credential_state();
    zassert_equal(crux_access_init(&policy), CRUX_ACCESS_OK);
    zassert_equal(crux_security_event_start(), CRUX_OK);
    return NULL;
}

ZTEST_SUITE(management_module, NULL, start_services, NULL, NULL, NULL);

ZTEST(management_module, test_01_the_store_started_on_the_module_s_core)
{
    zassert_not_equal(state_at_start, CRUX_CREDENTIAL_STATE_UNUSABLE);
    zassert_equal(crux_credential_state(), state_at_start);
}

ZTEST(management_module, test_02_first_account_login_and_refusals)
{
    static const crux_access_origin_t serial = {.channel = CRUX_ACCESS_CHANNEL_SERIAL};
    static const crux_access_origin_t web = {
        .channel = CRUX_ACCESS_CHANNEL_WEB, .source = {192, 0, 2, 1}, .source_length = 4};
    crux_access_session_t guest = {0};
    uint32_t retry_after_ms = 0;

    zassert_equal(crux_access_login_password(ADMIN, ADMIN_PASSWORD, strlen(ADMIN_PASSWORD), &web,
                                             &admin, &retry_after_ms),
                  CRUX_ACCESS_ERR_DENIED, "no account yet");
    zassert_equal(crux_access_provision_first_account(ADMIN, ADMIN_PASSWORD, strlen(ADMIN_PASSWORD),
                                                      "administrator", &serial),
                  CRUX_ACCESS_OK);
    zassert_equal(crux_access_login_password(ADMIN, WRONG_PASSWORD, strlen(WRONG_PASSWORD), &web,
                                             &guest, &retry_after_ms),
                  CRUX_ACCESS_ERR_DENIED);
    zassert_equal(crux_access_login_password(ADMIN, ADMIN_PASSWORD, strlen(ADMIN_PASSWORD), &web,
                                             &admin, &retry_after_ms),
                  CRUX_ACCESS_OK);
    zassert_equal(crux_access_check(admin, TEST_PERMISSION), CRUX_ACCESS_OK);
    zassert_equal(crux_access_check(admin, "nothing.names.this"), CRUX_ACCESS_ERR_DENIED);
}

ZTEST(management_module, test_03_the_events_provider_of_access_is_the_module_s)
{
    zassert_equal(count_events(admin, CRUX_SECURITY_EVENT_ID_ACCESS_PROVISIONED), 1u);
    zassert_true(count_events(admin, CRUX_SECURITY_EVENT_ID_ACCESS_LOGIN_FAILED) >= 1u);
    zassert_true(count_events(admin, CRUX_SECURITY_EVENT_ID_ACCESS_LOGIN_OK) >= 1u);
}
