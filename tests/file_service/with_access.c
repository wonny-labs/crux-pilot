/* The service on the real Access core, with fake crypto and a fake File provider. The other suites
 * use a double for speed and control, so this one proves that the two contracts fit together. */
#include "access_test.h"
#include "fake_file.h"
#include "file_service_port.h"

void crux_file_service_port_event(crux_file_service_event_t event, crux_access_session_t session,
                                  const char *area, const char *name, const char *new_name,
                                  bool storage_limit)
{
    (void)event;
    (void)session;
    (void)area;
    (void)name;
    (void)new_name;
    (void)storage_limit;
}

static const char *const administrator_permissions[] = {CRUX_ACCESS_PERMISSION_MANAGE, "data.read",
                                                        "data.write"};
static const char *const viewer_permissions[] = {"data.read"};
static const char *const stranger_permissions[] = {"status.read"};
static const crux_access_role_t roles[] = {
    {"administrator", administrator_permissions, 3},
    {"viewer", viewer_permissions, 1},
    {"stranger", stranger_permissions, 1},
};
static const crux_access_policy_t policy = {roles, 3, 15u * 60u, 8u * 3600u, NULL};

static const crux_file_area_t area = {
    "data", 0, "data.read", "data.write", 1000, 2000, 5, NULL, CRUX_FILE_SERVICE_MANAGED};
static const crux_file_service_config_t config = {&area, 1, NULL, 0, true};

static crux_access_session_t login(const char *user, const char *password)
{
    crux_access_session_t session = {0};
    uint32_t retry_after_ms = 0;
    CHECK(crux_access_login_password(user, password, strlen(password), &access_test_web, &session,
                                     &retry_after_ms) == CRUX_ACCESS_OK);
    return session;
}

static size_t denials(void)
{
    size_t count = 0;
    for (size_t i = 0; i < access_fake_event_count(); i++) {
        count += access_fake_event(i)->type == CRUX_ACCESS_EVENT_PERMISSION_DENIED;
    }
    return count;
}

/* After an administrator resets a password the account must change it, and until then it may see
 * and do nothing here. */
static crux_access_session_t viewer_after_forced_change(crux_access_session_t administrator)
{
    const char *first = ACCESS_TEST_PASSWORD;
    const char *second = ACCESS_TEST_OTHER_PASSWORD;
    CHECK(crux_access_account_create(administrator, "vera", "viewer", first, strlen(first)) ==
          CRUX_ACCESS_OK);
    CHECK(crux_access_account_reset_password(administrator, "vera", first, strlen(first)) ==
          CRUX_ACCESS_OK);
    crux_access_session_t forced = login("vera", first);

    const crux_file_area_t *found = NULL;
    size_t cursor = 0;
    bool writable = true;
    size_t before = denials();
    CHECK(crux_file_service_area_next(forced, &cursor, &found, &writable) == CRUX_FILE_SERVICE_END);
    CHECK(denials() == before);
    crux_file_info_t info;
    CHECK(crux_file_service_stat(forced, "data", "doc", &info) == CRUX_FILE_SERVICE_ERR_NOT_FOUND);
    CHECK(denials() == before + 1);

    uint32_t retry_after_ms = 0;
    CHECK(crux_access_account_change_password(forced, first, strlen(first), second, strlen(second),
                                              &retry_after_ms) == CRUX_ACCESS_OK);
    crux_access_logout(&forced);
    return login("vera", second);
}

int main(void)
{
    access_fake_reset();
    crux_file_fake_reset();
    CHECK(crux_file_fake_bind_root(0, "/card/data") == CRUX_FILE_OK);
    CHECK(crux_access_init(&policy) == CRUX_ACCESS_OK);
    const char *password = ACCESS_TEST_PASSWORD;
    CHECK(crux_access_provision_first_account("root", password, strlen(password), "administrator",
                                              &access_test_serial) == CRUX_ACCESS_OK);
    crux_access_session_t administrator = login("root", password);
    CHECK(crux_file_service_start(&config) == CRUX_FILE_SERVICE_OK);

    /* An administrator sees the area as writable and can publish a file. */
    const crux_file_area_t *found = NULL;
    size_t cursor = 0;
    bool writable = false;
    CHECK(crux_file_service_area_next(administrator, &cursor, &found, &writable) ==
          CRUX_FILE_SERVICE_OK);
    CHECK(found == &area && writable);
    crux_file_service_transfer_t *upload = NULL;
    size_t count = 0;
    CHECK(crux_file_service_open(administrator, "data", "doc", CRUX_FILE_SERVICE_OPEN_MANAGED, 0,
                                 &upload) == CRUX_FILE_SERVICE_OK);
    CHECK(crux_file_service_write(administrator, upload, 0, "abc", 3, &count) ==
          CRUX_FILE_SERVICE_OK);
    CHECK(crux_file_service_commit(administrator, &upload) == CRUX_FILE_SERVICE_OK);
    CHECK(crux_file_fake_exists(0, "doc"));

    crux_access_session_t viewer = viewer_after_forced_change(administrator);

    /* A viewer reads, and a write is refused without hiding the area, with one recorded denial. */
    crux_file_info_t info = {0};
    CHECK(crux_file_service_stat(viewer, "data", "doc", &info) == CRUX_FILE_SERVICE_OK);
    CHECK(info.size == 3);
    size_t before = denials();
    CHECK(crux_file_service_remove(viewer, "data", "doc") == CRUX_FILE_SERVICE_ERR_DENIED);
    CHECK(denials() == before + 1);

    /* A role with no permission here gets what a missing area gets, and it is recorded. */
    CHECK(crux_access_account_create(administrator, "stan", "stranger", password,
                                     strlen(password)) == CRUX_ACCESS_OK);
    crux_access_session_t stranger = login("stan", password);
    before = denials();
    cursor = 0;
    CHECK(crux_file_service_area_next(stranger, &cursor, &found, &writable) ==
          CRUX_FILE_SERVICE_END);
    CHECK(denials() == before);
    CHECK(crux_file_service_stat(stranger, "data", "doc", &info) ==
          CRUX_FILE_SERVICE_ERR_NOT_FOUND);
    CHECK(denials() == before + 1);

    /* Another session's transfer is refused, a logout fails the next chunk, and close still works.
     */
    crux_file_service_transfer_t *reader = NULL;
    char value[4];
    CHECK(crux_file_service_open(viewer, "data", "doc", CRUX_FILE_SERVICE_OPEN_READ, 0, &reader) ==
          CRUX_FILE_SERVICE_OK);
    CHECK(crux_file_service_read(administrator, reader, 0, value, 1, &count) ==
          CRUX_FILE_SERVICE_ERR_DENIED);
    CHECK(crux_file_service_read(viewer, reader, 0, value, 3, &count) == CRUX_FILE_SERVICE_OK);
    CHECK(count == 3 && memcmp(value, "abc", 3) == 0);
    crux_access_session_t ended = viewer;
    crux_access_logout(&viewer);
    CHECK(crux_file_service_read(ended, reader, 0, value, 1, &count) ==
          CRUX_FILE_SERVICE_ERR_SESSION_INVALID);
    CHECK(crux_file_service_close(&reader) == CRUX_FILE_SERVICE_OK);

    return access_test_finish("file service with access");
}
