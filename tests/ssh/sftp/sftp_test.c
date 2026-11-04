/*
 * The SFTP file operations on the real file service and the real Access core, with fake File and
 * crypto underneath, reached through the fake provider. It holds what the port owes the file
 * service. What wolfSSH does with a result is shown by tests/ssh/sftp/openssh, not here.
 */
#include "access_test.h"
#include "crux_ssh_attachment.h"
#include "crux_ssh_callbacks.h"
#include "crux_ssh_sftp_ops.h"
#include "fake_attachment.h"
#include "fake_file.h"
#include "fake_ports.h"
#include "fake_provider.h"
#include "file_service_port.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#define O_READ CRUX_SSH_SFTP_O_RDONLY
#define O_WRITE CRUX_SSH_SFTP_O_WRONLY
#define O_CREATE (O_WRITE | CRUX_SSH_SFTP_O_CREAT)
#define O_REPLACE (O_CREATE | CRUX_SSH_SFTP_O_TRUNC)
#define O_NEW (O_CREATE | CRUX_SSH_SFTP_O_EXCL)

enum { ROOT_DATA, ROOT_LOGS, ROOT_INBOX, ROOT_WEB };

static const crux_file_area_t areas[] = {
    {"data", ROOT_DATA, "data.read", "data.write", 200, 400, 4, ".filepart",
     CRUX_FILE_SERVICE_DIRECT},
    {"logs", ROOT_LOGS, "logs.read", NULL, 200, 400, 4, NULL, 0},
    {"inbox", ROOT_INBOX, "inbox.read", "inbox.write", 200, 400, 4, NULL, CRUX_FILE_SERVICE_DIRECT},
    {"web", ROOT_WEB, "data.read", "data.write", 200, 400, 4, NULL, CRUX_FILE_SERVICE_MANAGED},
};
static const crux_file_service_config_t service_config = {areas, 4, NULL, 0, true};

static const char *const admin_permissions[] = {CRUX_ACCESS_PERMISSION_MANAGE, "data.read",
                                                "data.write", "logs.read"};
static const char *const viewer_permissions[] = {"data.read", "logs.read"};
static const char *const dropper_permissions[] = {"inbox.write"};
static const char *const stranger_permissions[] = {"status.read"};
static const crux_access_role_t roles[] = {
    {"administrator", admin_permissions, 4},
    {"viewer", viewer_permissions, 2},
    {"dropper", dropper_permissions, 1},
    {"stranger", stranger_permissions, 1},
};
#define IDLE_LIMIT_MS 60000u
static const crux_access_policy_t access_policy = {roles, 4, IDLE_LIMIT_MS / 1000u, 3600u, NULL};

/* ---- The event port of the file service ---- */

static unsigned rejected_names;
static unsigned created_events;
static unsigned partial_removals;
static bool rejected_event_had_text;

// NOLINTBEGIN(bugprone-easily-swappable-parameters): the private port fixes this signature
void crux_file_service_port_event(crux_file_service_event_t event, crux_access_session_t session,
                                  const char *area, const char *name, const char *new_name,
                                  bool storage_limit)
// NOLINTEND(bugprone-easily-swappable-parameters)
{
    (void)session;
    (void)area;
    (void)storage_limit;
    switch (event) {
    case CRUX_FILE_SERVICE_EVENT_NAME_REJECTED:
        rejected_names++;
        rejected_event_had_text = rejected_event_had_text || name != NULL || new_name != NULL;
        break;
    case CRUX_FILE_SERVICE_EVENT_CREATED:
        created_events++;
        break;
    case CRUX_FILE_SERVICE_EVENT_PARTIAL_REMOVED:
        partial_removals++;
        break;
    default:
        break;
    }
}

/* ---- The callbacks that the access adapter of #192 will own ---- */

static crux_ssh_auth_result_t on_password(void *context, const crux_ssh_peer_t *peer,
                                          const char *user, const uint8_t *password, size_t length,
                                          crux_ssh_attachment_t **attachment)
{
    (void)context;
    (void)peer;
    crux_access_session_t session = {0};
    uint32_t retry_after_ms = 0;
    if (crux_access_login_password(user, (const char *)password, length, &access_test_ssh, &session,
                                   &retry_after_ms) != CRUX_ACCESS_OK) {
        return CRUX_SSH_AUTH_DENIED;
    }
    *attachment = crux_ssh_fake_attachment(session);
    return *attachment != NULL ? CRUX_SSH_AUTH_ACCEPTED : CRUX_SSH_AUTH_DENIED;
}

static crux_ssh_auth_result_t on_key_check(void *context, const crux_ssh_peer_t *peer,
                                           const char *user, crux_ssh_key_type_t key_type,
                                           const uint8_t *key, size_t key_length,
                                           bool signed_request)
{
    (void)context;
    (void)peer;
    (void)user;
    (void)key_type;
    (void)key;
    (void)key_length;
    (void)signed_request;
    return CRUX_SSH_AUTH_DENIED;
}

static crux_ssh_auth_result_t on_key_verified(void *context, const crux_ssh_peer_t *peer,
                                              const char *user, crux_ssh_key_type_t key_type,
                                              const uint8_t *key, size_t key_length,
                                              crux_ssh_attachment_t **attachment)
{
    (void)context;
    (void)peer;
    (void)user;
    (void)key_type;
    (void)key;
    (void)key_length;
    (void)attachment;
    return CRUX_SSH_AUTH_DENIED;
}

static void on_key_failed(void *context, const crux_ssh_peer_t *peer, const char *user)
{
    (void)context;
    (void)peer;
    (void)user;
}

static int on_shell(void *context, crux_ssh_attachment_t *attachment, crux_ssh_channel_t *channel,
                    bool terminal)
{
    (void)context;
    (void)attachment;
    (void)channel;
    (void)terminal;
    return 255;
}

static int on_exec(void *context, crux_ssh_attachment_t *attachment, crux_ssh_channel_t *channel,
                   const char *command, size_t length)
{
    (void)context;
    (void)attachment;
    (void)channel;
    (void)command;
    (void)length;
    return 255;
}

/* As #192 answers it: a query that does not refresh the idle timer. */
static bool on_session_alive(void *context, crux_ssh_attachment_t *attachment)
{
    (void)context;
    char user[CRUX_ACCESS_NAME_SIZE];
    return crux_access_session_user(crux_ssh_attachment_session(attachment), user, sizeof(user)) ==
           CRUX_ACCESS_OK;
}

static void on_connection_closed(void *context, crux_ssh_attachment_t *attachment)
{
    (void)context;
    crux_access_session_t session = crux_ssh_attachment_session(attachment);
    crux_access_logout(&session);
}

static const crux_ssh_callbacks_t callbacks = {
    .password = on_password,
    .key_check = on_key_check,
    .key_verified = on_key_verified,
    .key_failed = on_key_failed,
    .shell = on_shell,
    .exec = on_exec,
    .sftp_open = crux_ssh_sftp_open,
    .sftp_close = crux_ssh_sftp_close,
    .session_alive = on_session_alive,
    .connection_closed = on_connection_closed,
};

/* ---- A session through the fake provider ---- */

static const crux_ssh_test_client_t *client;

static void create_account(crux_access_session_t admin, const char *user, const char *role)
{
    const char *first = ACCESS_TEST_OTHER_PASSWORD;
    CHECK(crux_access_account_create(admin, user, role, first, strlen(first)) == CRUX_ACCESS_OK);
}

/* Everything but the medium, which the runner empties. Each case runs in its own child, because a
 * file service starts once. */
static void fresh_world(void)
{
    crux_ssh_fake_reset();
    crux_ssh_fake_attachment_reset();
    access_test_boot(&access_policy);
    CHECK(crux_access_provision_first_account("root", ACCESS_TEST_PASSWORD,
                                              strlen(ACCESS_TEST_PASSWORD), "administrator",
                                              &access_test_serial) == CRUX_ACCESS_OK);
    crux_access_session_t admin = access_test_session("root", ACCESS_TEST_PASSWORD, &access_test_web);
    create_account(admin, "vera", "viewer");
    create_account(admin, "dora", "dropper");
    create_account(admin, "stan", "stranger");
    crux_access_logout(&admin);
    CHECK(crux_file_service_start(&service_config) == CRUX_FILE_SERVICE_OK);
    access_fake_events_clear();

    static const crux_ssh_config_t server = {
        .port = 22,
        .max_connections = 4,
        .max_auth_attempts = 3,
        .auth_timeout_ms = 1000,
        .io_timeout_ms = 1000,
        .host_key_type = CRUX_SSH_KEY_ED25519,
        .host_key = (const uint8_t *)"host key",
        .host_key_length = 8,
    };
    client = crux_ssh_fake_client();
    CHECK(client->provider->start(&server, &callbacks) == CRUX_OK);
}

static int connect_as(const char *user)
{
    int connection = -1;
    const char *password = strcmp(user, "root") == 0 ? ACCESS_TEST_PASSWORD
                                                     : ACCESS_TEST_OTHER_PASSWORD;
    CHECK(client->connect(&client->peer_a, &connection));
    CHECK(client->password(connection, user, strlen(user), password));
    CHECK(client->open_channel(connection, "session"));
    CHECK(client->channel_request(connection, "subsystem", "sftp", 4));
    CHECK(client->is_connected(connection));
    return connection;
}

static void *open_as(const char *user)
{
    void *fs = client->file_handle(connect_as(user));
    CHECK(fs != NULL);
    return fs;
}

/* Puts a file on the medium past the service, as one that was already there. */
static void place(crux_file_root_t root, const char *name, const char *content)
{
    crux_file_t *file = NULL;
    size_t count = 0;
    CHECK(crux_file_open_at(root, name, CRUX_FILE_CREATE_REPLACE, &file) == CRUX_FILE_OK);
    CHECK(crux_file_write(file, content, strlen(content), &count) == CRUX_FILE_OK);
    CHECK(crux_file_close(&file) == CRUX_FILE_OK);
}

static uint64_t size_on_medium(crux_file_root_t root, const char *name)
{
    crux_file_info_t info = {0};
    CHECK(crux_file_stat(root, name, &info) == CRUX_FILE_OK);
    return info.size;
}

static const uint32_t OFFSET_0[2] = {0, 0};

static int write_text(void *fs, int fd, const char *text, uint32_t offset)
{
    const uint32_t at[2] = {offset, 0};
    return crux_ssh_sftp_file_pwrite(fs, fd, text, (uint32_t)strlen(text), at);
}

static size_t denials(void)
{
    return access_fake_event_count_of(CRUX_ACCESS_EVENT_PERMISSION_DENIED);
}

/* ---- The session handle ---- */

static void case_open_needs_a_live_session(void)
{
    fresh_world();
    crux_ssh_file_handle_t *handle = NULL;
    crux_access_session_t none = {0};
    crux_ssh_attachment_t *attachment = crux_ssh_fake_attachment(none);

    CHECK(crux_ssh_sftp_open(NULL, attachment, &handle) != CRUX_OK);
    CHECK(handle == NULL);
    CHECK(crux_ssh_sftp_open(NULL, NULL, &handle) != CRUX_OK);
    CHECK(handle == NULL);

    crux_access_session_t vera = access_test_session("vera", ACCESS_TEST_OTHER_PASSWORD,
                                                     &access_test_ssh);
    CHECK(crux_ssh_sftp_open(NULL, crux_ssh_fake_attachment(vera), NULL) != CRUX_OK);
}

static void case_session_handles_are_bounded_and_come_back(void)
{
    fresh_world();
    crux_access_session_t vera = access_test_session("vera", ACCESS_TEST_OTHER_PASSWORD,
                                                     &access_test_ssh);
    crux_ssh_attachment_t *attachment = crux_ssh_fake_attachment(vera);
    crux_ssh_file_handle_t *handles[8] = {NULL};
    size_t opened = 0;

    while (opened < 8 && crux_ssh_sftp_open(NULL, attachment, &handles[opened]) == CRUX_OK) {
        CHECK(handles[opened] != NULL);
        opened++;
    }
    CHECK(opened >= 1 && opened < 8);
    crux_ssh_file_handle_t *none = NULL;
    CHECK(crux_ssh_sftp_open(NULL, attachment, &none) == CRUX_ERR_NO_RESOURCES);
    CHECK(none == NULL);
    crux_ssh_sftp_close(NULL, attachment, handles[0]);
    CHECK(crux_ssh_sftp_open(NULL, attachment, &none) == CRUX_OK);
    CHECK(none != NULL);
    crux_ssh_sftp_close(NULL, attachment, none);
    for (size_t i = 1; i < opened; i++) {
        crux_ssh_sftp_close(NULL, attachment, handles[i]);
    }
}

static void case_a_handle_works_only_while_it_is_open(void)
{
    fresh_world();
    place(ROOT_DATA, "doc", "abc");
    crux_ssh_sftp_attributes_t info;
    int stale = crux_ssh_sftp_file_open(NULL, "/data/doc", O_READ);
    CHECK(stale < 0);
    CHECK(crux_ssh_sftp_attributes(NULL, "/data/doc", &info) < 0);

    int connection = connect_as("root");
    void *fs = client->file_handle(connection);
    int fd = crux_ssh_sftp_file_open(fs, "/data/doc", O_READ);
    CHECK(fd >= 0);
    client->end_sftp(connection);
    /* wolfSSH can close what is left after sftp_close, with the same pointer, once the slot may
     * belong to another connection. Neither call may reach that connection's files. */
    CHECK(crux_ssh_sftp_file_close(fs, fd) < 0);
    void *next = open_as("root");
    CHECK(next == fs);
    int other = crux_ssh_sftp_file_open(next, "/data/doc", O_READ);
    CHECK(other >= 0);
    CHECK(crux_ssh_sftp_file_close(fs, fd) < 0);
    char buffer[4];
    CHECK(crux_ssh_sftp_file_pread(next, other, buffer, sizeof(buffer), OFFSET_0) == 3);
    CHECK(crux_ssh_sftp_file_close(next, other) == 0);
}

/* ---- The virtual root ---- */

static void read_all(void *fs, const char *path, char names[][CRUX_SSH_SFTP_NAME_CAPACITY],
                     crux_ssh_sftp_attributes_t *attributes, size_t capacity, size_t *count)
{
    int dir = -1;
    *count = 0;
    CHECK(crux_ssh_sftp_dir_open(fs, path, &dir) == 0);
    for (;;) {
        char name[CRUX_SSH_SFTP_NAME_CAPACITY];
        crux_ssh_sftp_attributes_t found = {0};
        int result = crux_ssh_sftp_dir_read(fs, dir, name, sizeof(name), &found);
        CHECK(result == 0 || result == 1);
        if (result != 0) {
            break;
        }
        CHECK(*count < capacity);
        strcpy(names[*count], name);
        attributes[*count] = found;
        (*count)++;
    }
    /* The end repeats, and a closed listing is gone. */
    char after[CRUX_SSH_SFTP_NAME_CAPACITY];
    crux_ssh_sftp_attributes_t ignored;
    CHECK(crux_ssh_sftp_dir_read(fs, dir, after, sizeof(after), &ignored) == 1);
    CHECK(crux_ssh_sftp_dir_close(fs, dir) == 0);
    CHECK(crux_ssh_sftp_dir_read(fs, dir, after, sizeof(after), &ignored) < 0);
}

static void case_root_lists_only_visible_areas_with_no_denial(void)
{
    fresh_world();
    char names[8][CRUX_SSH_SFTP_NAME_CAPACITY];
    crux_ssh_sftp_attributes_t attributes[8];
    size_t count = 0;

    read_all(open_as("root"), "/", names, attributes, 8, &count);
    CHECK(count == 3); /* data, logs and web */
    CHECK(strcmp(names[0], "data") == 0);
    CHECK(attributes[0].mode == (CRUX_SSH_SFTP_MODE_DIRECTORY | 0755u));
    CHECK(strcmp(names[1], "logs") == 0);
    CHECK(attributes[1].mode == (CRUX_SSH_SFTP_MODE_DIRECTORY | 0555u));

    read_all(open_as("vera"), "/", names, attributes, 8, &count);
    CHECK(count == 3);
    CHECK(attributes[0].mode == (CRUX_SSH_SFTP_MODE_DIRECTORY | 0555u));

    read_all(open_as("dora"), "/", names, attributes, 8, &count);
    CHECK(count == 1);
    CHECK(strcmp(names[0], "inbox") == 0);
    CHECK(attributes[0].mode == (CRUX_SSH_SFTP_MODE_DIRECTORY | 0300u));

    read_all(open_as("stan"), "/", names, attributes, 8, &count);
    CHECK(count == 0);
    CHECK(denials() == 0);
}

static void case_area_lists_files_and_hides_what_the_service_hides(void)
{
    fresh_world();
    place(ROOT_DATA, "one", "1");
    place(ROOT_DATA, "two", "22");
    crux_file_fake_add_directory(ROOT_DATA, "subdir");
    crux_file_fake_add_raw(ROOT_DATA, "has space");
    char names[8][CRUX_SSH_SFTP_NAME_CAPACITY];
    crux_ssh_sftp_attributes_t attributes[8];
    size_t count = 0;

    void *fs = open_as("root");
    read_all(fs, "/data", names, attributes, 8, &count);
    CHECK(count == 2);
    for (size_t i = 0; i < count; i++) {
        CHECK(attributes[i].mode == (CRUX_SSH_SFTP_MODE_FILE | 0644u));
        CHECK(attributes[i].size == (strcmp(names[i], "one") == 0 ? 1u : 2u));
    }
    /* The rights come from this session, not from the file. */
    read_all(open_as("vera"), "/data", names, attributes, 8, &count);
    CHECK(count == 2);
    CHECK(attributes[0].mode == (CRUX_SSH_SFTP_MODE_FILE | 0444u));

    /* An empty area is an empty listing, and WinSCP then reads the area's parent. */
    read_all(fs, "/logs", names, attributes, 8, &count);
    CHECK(count == 0);
    crux_ssh_sftp_attributes_t parent = {0};
    CHECK(crux_ssh_sftp_attributes(fs, "/", &parent) == 0);
    CHECK(parent.mode == (CRUX_SSH_SFTP_MODE_DIRECTORY | 0555u));
}

static void case_listing_refusals(void)
{
    fresh_world();
    place(ROOT_DATA, "one", "1");
    int dir = -1;

    /* Neither permission, or no such area: one result, and only the first tells the service. */
    void *stranger = open_as("stan");
    CHECK(crux_ssh_sftp_dir_open(stranger, "/data", &dir) < 0);
    CHECK(denials() == 1);
    CHECK(crux_ssh_sftp_dir_open(stranger, "/missing", &dir) < 0);
    CHECK(denials() == 1);

    /* Write permission alone sees the area and cannot list it. */
    void *dropper = open_as("dora");
    CHECK(crux_ssh_sftp_dir_open(dropper, "/inbox", &dir) < 0);

    /* A file is not a directory, and neither is anything the parser refuses. */
    void *fs = open_as("root");
    CHECK(crux_ssh_sftp_dir_open(fs, "/data/one", &dir) < 0);
    CHECK(crux_ssh_sftp_dir_open(fs, "/data/one/deeper", &dir) < 0);

    /* The listing handle of the service is held until the listing ends, so a second one is busy. */
    int first = -1;
    CHECK(crux_ssh_sftp_dir_open(fs, "/data", &first) == 0);
    CHECK(crux_ssh_sftp_dir_open(fs, "/logs", &dir) < 0);
    /* The virtual root needs no handle. */
    int root = -1;
    CHECK(crux_ssh_sftp_dir_open(fs, "/", &root) == 0);
    CHECK(crux_ssh_sftp_dir_close(fs, root) == 0);
    char name[CRUX_SSH_SFTP_NAME_CAPACITY];
    crux_ssh_sftp_attributes_t found;
    CHECK(crux_ssh_sftp_dir_read(fs, first, name, sizeof(name), &found) == 0);
    CHECK(crux_ssh_sftp_dir_read(fs, first, name, sizeof(name), &found) == 1);
    /* Having read to the end, the listing has given the handle back, though it is not closed. */
    int second = -1;
    CHECK(crux_ssh_sftp_dir_open(fs, "/logs", &second) == 0);
    CHECK(crux_ssh_sftp_dir_close(fs, second) == 0);
    CHECK(crux_ssh_sftp_dir_close(fs, first) == 0);
}

static void case_listing_error_ends_the_listing(void)
{
    fresh_world();
    place(ROOT_DATA, "one", "1");
    void *fs = open_as("root");
    int dir = -1;
    CHECK(crux_ssh_sftp_dir_open(fs, "/data", &dir) == 0);
    crux_file_fake_fail(CRUX_FILE_FAKE_DIR_READ, 0, CRUX_FILE_ERR_IO);
    char name[CRUX_SSH_SFTP_NAME_CAPACITY];
    crux_ssh_sftp_attributes_t found;
    CHECK(crux_ssh_sftp_dir_read(fs, dir, name, sizeof(name), &found) < 0);
    CHECK(crux_ssh_sftp_dir_read(fs, dir, name, sizeof(name), &found) == 1);
    int next = -1;
    CHECK(crux_ssh_sftp_dir_open(fs, "/data", &next) == 0);
    CHECK(crux_ssh_sftp_dir_close(fs, next) == 0);
    CHECK(crux_ssh_sftp_dir_close(fs, dir) == 0);
}

static void case_attributes(void)
{
    fresh_world();
    place(ROOT_DATA, "doc", "abc");
    crux_ssh_sftp_attributes_t info = {0};

    void *fs = open_as("root");
    CHECK(crux_ssh_sftp_attributes(fs, "/", &info) == 0);
    CHECK(info.mode == (CRUX_SSH_SFTP_MODE_DIRECTORY | 0555u));
    CHECK(crux_ssh_sftp_attributes(fs, "/data", &info) == 0);
    CHECK(info.mode == (CRUX_SSH_SFTP_MODE_DIRECTORY | 0755u));
    CHECK(crux_ssh_sftp_attributes(fs, "/data/", &info) == 0);
    CHECK(crux_ssh_sftp_attributes(fs, "/data/doc", &info) == 0);
    CHECK(info.mode == (CRUX_SSH_SFTP_MODE_FILE | 0644u));
    CHECK(info.size == 3);
    CHECK(crux_ssh_sftp_attributes(fs, "/data/missing", &info) < 0);
    CHECK(crux_ssh_sftp_attributes(fs, "/missing", &info) < 0);

    void *viewer = open_as("vera");
    CHECK(crux_ssh_sftp_attributes(viewer, "/data/doc", &info) == 0);
    CHECK(info.mode == (CRUX_SSH_SFTP_MODE_FILE | 0444u));

    /* A hidden area looks like a missing one, with one denial when a name in it is asked for. */
    void *stranger = open_as("stan");
    size_t before = denials();
    CHECK(crux_ssh_sftp_attributes(stranger, "/data", &info) < 0);
    CHECK(denials() == before);
    CHECK(crux_ssh_sftp_attributes(stranger, "/data/doc", &info) < 0);
    CHECK(denials() == before + 1);
    CHECK(crux_ssh_sftp_attributes(stranger, "/", &info) == 0);
    CHECK(crux_ssh_sftp_attributes(stranger, "/missing", &info) < 0);
    CHECK(denials() == before + 1);
}

/* ---- Traversal and names ---- */

static void case_paths_outside_the_rules_reach_nothing(void)
{
    fresh_world();
    place(ROOT_DATA, "doc", "abc");
    crux_file_fake_add_raw(ROOT_DATA, ".crux-up-0");
    /* wolfSSH normalises a path lexically before the port sees it. These are what must hold if it
     * did not, because the port is the second defence behind nothing else. */
    static const char *const refused[] = {
        "",
        "data",
        "data/doc",
        "//",
        "//data",
        "/data//doc",
        "/data/./doc",
        "/data/../data/doc",
        "/../data/doc",
        "/data/..",
        "/data/.",
        "/..",
        "/.",
        "/data/doc/",
        "/data/doc/..",
        "/data/sub/doc",
        "/data\\doc",
        "\\data\\doc",
        "/data/..\\doc",
        "/data/has space",
        "/data/.crux-up-0",
        "/DATA/doc",
        "/data/doc\n",
        "/data/\xc3\xa9",
        "/averyveryverylongareaname/doc",
        "/data/aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa",
    };
    void *fs = open_as("root");
    crux_ssh_sftp_attributes_t info;
    int dir = -1;

    for (size_t i = 0; i < sizeof(refused) / sizeof(refused[0]); i++) {
        const char *path = refused[i];
        bool is_area = strcmp(path, "/data/doc/") == 0;
        CHECK(crux_ssh_sftp_file_open(fs, path, O_READ) < 0);
        CHECK(crux_ssh_sftp_file_open(fs, path, O_REPLACE) < 0);
        CHECK(crux_ssh_sftp_remove(fs, path) < 0);
        CHECK(crux_ssh_sftp_rename(fs, path, "/data/moved") < 0);
        CHECK(crux_ssh_sftp_rename(fs, "/data/doc", path) < 0);
        if (!is_area) {
            CHECK(crux_ssh_sftp_dir_open(fs, path, &dir) < 0);
            CHECK(crux_ssh_sftp_attributes(fs, path, &info) < 0);
            CHECK(crux_ssh_sftp_set_attributes(fs, path) < 0);
        }
    }
    CHECK(size_on_medium(ROOT_DATA, "doc") == 3);
    CHECK(!crux_file_fake_exists(ROOT_DATA, "moved"));
    CHECK(crux_ssh_sftp_file_open(NULL, "/data/doc", O_READ) < 0);
    CHECK(crux_ssh_sftp_file_open(fs, NULL, O_READ) < 0);
    CHECK(crux_ssh_sftp_attributes(fs, NULL, &info) < 0);
    CHECK(crux_ssh_sftp_remove(fs, NULL) < 0);
    CHECK(crux_ssh_sftp_rename(fs, NULL, "/data/x") < 0);
    CHECK(crux_ssh_sftp_rename(fs, "/data/doc", NULL) < 0);
}

static void case_a_refused_name_is_recorded_once_with_no_text(void)
{
    fresh_world();
    void *fs = open_as("root");

    CHECK(crux_ssh_sftp_file_open(fs, "/data/my file.txt", O_NEW) < 0);
    CHECK(rejected_names == 1);
    CHECK(!crux_file_fake_exists(ROOT_DATA, "my file.txt"));
    CHECK(crux_ssh_sftp_remove(fs, "/data/my file.txt") < 0);
    CHECK(rejected_names == 2);
    CHECK(crux_ssh_sftp_rename(fs, "/data/my file.txt", "/data/plain") < 0);
    CHECK(rejected_names == 3);
    crux_ssh_sftp_attributes_t info;
    CHECK(crux_ssh_sftp_attributes(fs, "/data/my file.txt", &info) < 0);
    CHECK(rejected_names == 4);
    CHECK(!rejected_event_had_text);

    /* A name in an area that does not exist has no area to record it against. */
    CHECK(crux_ssh_sftp_file_open(fs, "/missing/my file.txt", O_NEW) < 0);
    CHECK(rejected_names == 4);

    /* A session that cannot see the area learns nothing, and it is the permission that is refused.
     */
    void *stranger = open_as("stan");
    size_t before = denials();
    CHECK(crux_ssh_sftp_file_open(stranger, "/data/my file.txt", O_NEW) < 0);
    CHECK(rejected_names == 4);
    CHECK(denials() == before + 1);
}

/* ---- Open modes ---- */

static void case_open_flags_map_to_the_service_modes(void)
{
    fresh_world();
    void *fs = open_as("root");
    place(ROOT_DATA, "doc", "abcdef");

    /* WRITE alone needs the file to exist, and it keeps the content. */
    CHECK(crux_ssh_sftp_file_open(fs, "/data/none", O_WRITE) < 0);
    CHECK(!crux_file_fake_exists(ROOT_DATA, "none"));
    int fd = crux_ssh_sftp_file_open(fs, "/data/doc", O_WRITE);
    CHECK(fd >= 0);
    CHECK(crux_ssh_sftp_file_close(fs, fd) == 0);
    CHECK(size_on_medium(ROOT_DATA, "doc") == 6);

    /* WRITE and CREAT make a file or keep one. */
    fd = crux_ssh_sftp_file_open(fs, "/data/made", O_CREATE);
    CHECK(fd >= 0);
    CHECK(crux_ssh_sftp_file_close(fs, fd) == 0);
    CHECK(crux_file_fake_exists(ROOT_DATA, "made"));
    fd = crux_ssh_sftp_file_open(fs, "/data/doc", O_CREATE);
    CHECK(fd >= 0);
    CHECK(crux_ssh_sftp_file_close(fs, fd) == 0);
    CHECK(size_on_medium(ROOT_DATA, "doc") == 6);

    /* EXCL refuses one that exists, and leaves it. */
    CHECK(crux_ssh_sftp_file_open(fs, "/data/doc", O_NEW) < 0);
    CHECK(size_on_medium(ROOT_DATA, "doc") == 6);
    fd = crux_ssh_sftp_file_open(fs, "/data/fresh", O_NEW);
    CHECK(fd >= 0);
    CHECK(crux_ssh_sftp_file_close(fs, fd) == 0);

    /* TRUNC empties it. */
    fd = crux_ssh_sftp_file_open(fs, "/data/doc", O_REPLACE);
    CHECK(fd >= 0);
    CHECK(crux_ssh_sftp_file_close(fs, fd) == 0);
    CHECK(size_on_medium(ROOT_DATA, "doc") == 0);
}

static void case_open_flags_that_mean_nothing_are_refused(void)
{
    fresh_world();
    place(ROOT_DATA, "doc", "abcdef");
    void *fs = open_as("root");

    /* Each would have to invent a meaning, so none reaches the service. */
    CHECK(crux_ssh_sftp_file_open(fs, "/data/doc", 0) < 0);
    CHECK(crux_ssh_sftp_file_open(fs, "/data/new1", CRUX_SSH_SFTP_O_CREAT) < 0);
    CHECK(crux_ssh_sftp_file_open(fs, "/data/new2", O_READ | CRUX_SSH_SFTP_O_CREAT) < 0);
    CHECK(crux_ssh_sftp_file_open(fs, "/data/doc", O_READ | CRUX_SSH_SFTP_O_TRUNC) < 0);
    CHECK(crux_ssh_sftp_file_open(fs, "/data/doc", O_READ | CRUX_SSH_SFTP_O_APPEND) < 0);
    CHECK(crux_ssh_sftp_file_open(fs, "/data/doc", O_WRITE | CRUX_SSH_SFTP_O_TRUNC) < 0);
    CHECK(crux_ssh_sftp_file_open(fs, "/data/doc", O_WRITE | CRUX_SSH_SFTP_O_EXCL) < 0);
    CHECK(size_on_medium(ROOT_DATA, "doc") == 6);
    CHECK(!crux_file_fake_exists(ROOT_DATA, "new1"));
    CHECK(!crux_file_fake_exists(ROOT_DATA, "new2"));
}

static void case_read_and_write_together(void)
{
    fresh_world();
    place(ROOT_DATA, "doc", "abcdef");
    void *fs = open_as("root");

    int fd = crux_ssh_sftp_file_open(fs, "/data/doc", CRUX_SSH_SFTP_O_RDWR);
    CHECK(fd >= 0);
    char buffer[6];
    CHECK(crux_ssh_sftp_file_pread(fs, fd, buffer, sizeof(buffer), OFFSET_0) == 6);
    CHECK(memcmp(buffer, "abcdef", 6) == 0);
    CHECK(write_text(fs, fd, "XY", 2) == 2);
    CHECK(crux_ssh_sftp_file_pread(fs, fd, buffer, sizeof(buffer), OFFSET_0) == 6);
    CHECK(memcmp(buffer, "abXYef", 6) == 0);
    CHECK(crux_ssh_sftp_file_close(fs, fd) == 0);
    CHECK(crux_ssh_sftp_file_open(fs, "/data/none", CRUX_SSH_SFTP_O_RDWR) < 0);
}

/* ---- Permissions, in one place ---- */

static void case_permissions_come_from_the_service(void)
{
    fresh_world();
    place(ROOT_DATA, "doc", "abc");
    place(ROOT_LOGS, "log", "abc");
    crux_ssh_sftp_attributes_t info;

    /* A viewer reads, and every way of writing is refused with one denial and no change. */
    void *viewer = open_as("vera");
    int fd = crux_ssh_sftp_file_open(viewer, "/data/doc", O_READ);
    CHECK(fd >= 0);
    CHECK(crux_ssh_sftp_file_close(viewer, fd) == 0);
    static const int write_flags[] = {O_WRITE, O_CREATE, O_REPLACE, O_NEW, CRUX_SSH_SFTP_O_RDWR};
    for (size_t i = 0; i < sizeof(write_flags) / sizeof(write_flags[0]); i++) {
        size_t before = denials();
        CHECK(crux_ssh_sftp_file_open(viewer, "/data/doc", write_flags[i]) < 0);
        CHECK(denials() == before + 1);
    }
    size_t before = denials();
    CHECK(crux_ssh_sftp_remove(viewer, "/data/doc") < 0);
    CHECK(crux_ssh_sftp_rename(viewer, "/data/doc", "/data/other") < 0);
    CHECK(denials() == before + 2);
    CHECK(size_on_medium(ROOT_DATA, "doc") == 3);
    CHECK(!crux_file_fake_exists(ROOT_DATA, "other"));

    /* A read-only area refuses every session, administrators included, and no denial is due. */
    void *admin = open_as("root");
    before = denials();
    CHECK(crux_ssh_sftp_file_open(admin, "/logs/log", O_REPLACE) < 0);
    CHECK(crux_ssh_sftp_remove(admin, "/logs/log") < 0);
    CHECK(denials() == before);
    CHECK(size_on_medium(ROOT_LOGS, "log") == 3);

    /* An area that offers managed uploads only has no write mode for SFTP. */
    CHECK(crux_ssh_sftp_file_open(admin, "/web/page", O_REPLACE) < 0);
    CHECK(!crux_file_fake_exists(ROOT_WEB, "page"));

    /* Write without read: a drop box. */
    void *dropper = open_as("dora");
    fd = crux_ssh_sftp_file_open(dropper, "/inbox/drop", O_NEW);
    CHECK(fd >= 0);
    CHECK(write_text(dropper, fd, "secret", 0) == 6);
    char buffer[6];
    CHECK(crux_ssh_sftp_file_pread(dropper, fd, buffer, sizeof(buffer), OFFSET_0) < 0);
    CHECK(crux_ssh_sftp_file_close(dropper, fd) == 0);
    CHECK(crux_ssh_sftp_file_open(dropper, "/inbox/drop", O_READ) < 0);
    CHECK(crux_ssh_sftp_attributes(dropper, "/inbox/drop", &info) < 0);
    /* A timestamp after the upload must still pass, or WinSCP reports the upload as failed. */
    CHECK(crux_ssh_sftp_set_attributes(dropper, "/inbox/drop") == 0);

    /* A hidden area looks like a missing one for every operation. */
    void *stranger = open_as("stan");
    CHECK(crux_ssh_sftp_file_open(stranger, "/data/doc", O_READ) < 0);
    CHECK(crux_ssh_sftp_file_open(stranger, "/data/doc", O_REPLACE) < 0);
    CHECK(crux_ssh_sftp_remove(stranger, "/data/doc") < 0);
    CHECK(crux_ssh_sftp_set_attributes(stranger, "/data/doc") < 0);
    CHECK(size_on_medium(ROOT_DATA, "doc") == 3);
}

static void case_a_descriptor_cannot_cross_sessions(void)
{
    fresh_world();
    place(ROOT_DATA, "doc", "abc");
    void *one = open_as("root");
    void *two = open_as("root");
    int fd = crux_ssh_sftp_file_open(one, "/data/doc", O_READ);
    CHECK(fd >= 0);
    char buffer[3];
    CHECK(crux_ssh_sftp_file_pread(two, fd, buffer, sizeof(buffer), OFFSET_0) < 0);
    CHECK(crux_ssh_sftp_file_close(two, fd) < 0);
    CHECK(crux_ssh_sftp_file_pread(one, fd, buffer, sizeof(buffer), OFFSET_0) == 3);
    CHECK(crux_ssh_sftp_file_close(one, fd) == 0);
    CHECK(crux_ssh_sftp_file_close(one, fd) < 0);
    CHECK(crux_ssh_sftp_file_pread(one, fd, buffer, sizeof(buffer), OFFSET_0) < 0);
    CHECK(crux_ssh_sftp_file_pread(one, -1, buffer, sizeof(buffer), OFFSET_0) < 0);
    CHECK(crux_ssh_sftp_file_pread(one, 100000, buffer, sizeof(buffer), OFFSET_0) < 0);
}

/* ---- Reads, writes and limits ---- */

static void case_read_reports_the_end_as_zero(void)
{
    fresh_world();
    place(ROOT_DATA, "doc", "abcdef");
    void *fs = open_as("vera");
    int fd = crux_ssh_sftp_file_open(fs, "/data/doc", O_READ);
    CHECK(fd >= 0);
    char buffer[4];
    const uint32_t at_four[2] = {4, 0};
    const uint32_t at_end[2] = {6, 0};
    const uint32_t past_end[2] = {7, 0};
    const uint32_t far_past[2] = {0, 1};
    CHECK(crux_ssh_sftp_file_pread(fs, fd, buffer, sizeof(buffer), OFFSET_0) == 4);
    CHECK(memcmp(buffer, "abcd", 4) == 0);
    CHECK(crux_ssh_sftp_file_pread(fs, fd, buffer, sizeof(buffer), at_four) == 2);
    CHECK(memcmp(buffer, "ef", 2) == 0);
    CHECK(crux_ssh_sftp_file_pread(fs, fd, buffer, sizeof(buffer), at_end) == 0);
    CHECK(crux_ssh_sftp_file_pread(fs, fd, buffer, sizeof(buffer), past_end) == 0);
    CHECK(crux_ssh_sftp_file_pread(fs, fd, buffer, sizeof(buffer), far_past) == 0);
    /* Reading on after a read past the end still finds the file. */
    CHECK(crux_ssh_sftp_file_pread(fs, fd, buffer, sizeof(buffer), OFFSET_0) == 4);
    /* A storage failure is a failure, and it does not look like the end. */
    crux_file_fake_fail(CRUX_FILE_FAKE_READ, 0, CRUX_FILE_ERR_IO);
    CHECK(crux_ssh_sftp_file_pread(fs, fd, buffer, sizeof(buffer), at_four) < 0);
    CHECK(crux_ssh_sftp_file_close(fs, fd) == 0);
}

static void case_write_rules(void)
{
    fresh_world();
    void *fs = open_as("root");
    int fd = crux_ssh_sftp_file_open(fs, "/data/up", O_REPLACE);
    CHECK(fd >= 0);
    CHECK(write_text(fs, fd, "abc", 0) == 3);
    CHECK(write_text(fs, fd, "def", 3) == 3);
    /* In place, and at the end. A sparse write is refused, because File makes no sparse files. */
    CHECK(write_text(fs, fd, "X", 1) == 1);
    CHECK(write_text(fs, fd, "Z", 7) < 0);
    const uint32_t far[2] = {0, 1};
    CHECK(crux_ssh_sftp_file_pwrite(fs, fd, "Z", 1, far) < 0);
    /* A short write is reported as such, for wolfSSH to continue from. */
    crux_file_fake_limit_write(2);
    CHECK(write_text(fs, fd, "ghij", 6) == 2);
    crux_file_fake_limit_write(SIZE_MAX);
    CHECK(crux_ssh_sftp_file_close(fs, fd) == 0);
    CHECK(size_on_medium(ROOT_DATA, "up") == 8);
    /* A full medium and a failed write are failures. */
    fd = crux_ssh_sftp_file_open(fs, "/data/up", O_WRITE);
    CHECK(fd >= 0);
    crux_file_fake_fail(CRUX_FILE_FAKE_WRITE, 0, CRUX_FILE_ERR_NO_SPACE);
    CHECK(write_text(fs, fd, "k", 8) < 0);
    crux_file_fake_fail(CRUX_FILE_FAKE_WRITE, 0, CRUX_FILE_ERR_IO);
    CHECK(write_text(fs, fd, "k", 8) < 0);
    CHECK(crux_ssh_sftp_file_close(fs, fd) == 0);
}

static void case_append_writes_at_the_end(void)
{
    fresh_world();
    place(ROOT_DATA, "doc", "abc");
    void *fs = open_as("root");
    int fd = crux_ssh_sftp_file_open(fs, "/data/doc", O_WRITE | CRUX_SSH_SFTP_O_APPEND);
    CHECK(fd >= 0);
    CHECK(crux_ssh_sftp_file_append(fs, fd, "de", 2) == 2);
    CHECK(crux_ssh_sftp_file_append(fs, fd, "f", 1) == 1);
    CHECK(crux_ssh_sftp_file_close(fs, fd) == 0);
    CHECK(size_on_medium(ROOT_DATA, "doc") == 6);
    /* APPEND with TRUNC is what POSIX makes of it: empty, then at the end. */
    fd = crux_ssh_sftp_file_open(fs, "/data/doc", O_REPLACE | CRUX_SSH_SFTP_O_APPEND);
    CHECK(fd >= 0);
    CHECK(crux_ssh_sftp_file_append(fs, fd, "g", 1) == 1);
    CHECK(crux_ssh_sftp_file_close(fs, fd) == 0);
    CHECK(size_on_medium(ROOT_DATA, "doc") == 1);
    fd = crux_ssh_sftp_file_open(fs, "/data/doc", O_WRITE);
    CHECK(fd >= 0);
    CHECK(crux_ssh_sftp_file_append(fs, 99999, "h", 1) < 0);
    CHECK(crux_ssh_sftp_file_close(fs, fd) == 0);
}

static void case_limits_are_the_services(void)
{
    fresh_world();
    void *fs = open_as("root");
    static const char block[201] = "0123456789012345678901234567890123456789"
                                   "0123456789012345678901234567890123456789"
                                   "0123456789012345678901234567890123456789"
                                   "0123456789012345678901234567890123456789"
                                   "0123456789012345678901234567890123456789";

    /* max_file_bytes is 200: all of this write would cross it, and nothing of it lands. */
    int fd = crux_ssh_sftp_file_open(fs, "/data/big", O_REPLACE);
    CHECK(fd >= 0);
    const uint32_t at_start[2] = {0, 0};
    CHECK(crux_ssh_sftp_file_pwrite(fs, fd, block, 200, at_start) == 200);
    const uint32_t at_end[2] = {200, 0};
    CHECK(crux_ssh_sftp_file_pwrite(fs, fd, "x", 1, at_end) < 0);
    CHECK(crux_ssh_sftp_file_close(fs, fd) == 0);
    CHECK(size_on_medium(ROOT_DATA, "big") == 200);

    /* max_area_bytes is 400. */
    fd = crux_ssh_sftp_file_open(fs, "/data/big2", O_REPLACE);
    CHECK(fd >= 0);
    CHECK(crux_ssh_sftp_file_pwrite(fs, fd, block, 200, at_start) == 200);
    CHECK(crux_ssh_sftp_file_close(fs, fd) == 0);
    fd = crux_ssh_sftp_file_open(fs, "/data/over", O_REPLACE);
    CHECK(fd >= 0);
    CHECK(crux_ssh_sftp_file_pwrite(fs, fd, block, 1, at_start) < 0);
    CHECK(crux_ssh_sftp_file_close(fs, fd) == 0);

    /* max_entries is 4, and this area holds three files now. */
    fd = crux_ssh_sftp_file_open(fs, "/data/fourth", O_REPLACE);
    CHECK(fd >= 0);
    CHECK(crux_ssh_sftp_file_close(fs, fd) == 0);
    CHECK(crux_ssh_sftp_file_open(fs, "/data/fifth", O_REPLACE) < 0);
    CHECK(!crux_file_fake_exists(ROOT_DATA, "fifth"));

    /* Removing gives the room back. */
    CHECK(crux_ssh_sftp_remove(fs, "/data/over") == 0);
    fd = crux_ssh_sftp_file_open(fs, "/data/fifth", O_REPLACE);
    CHECK(fd >= 0);
    CHECK(crux_ssh_sftp_file_close(fs, fd) == 0);
}

static void case_open_files_are_bounded_for_each_session(void)
{
    fresh_world();
    place(ROOT_DATA, "a", "1");
    place(ROOT_DATA, "b", "1");
    place(ROOT_DATA, "c", "1");
    void *greedy = open_as("root");
    void *other = open_as("vera");

    int first = crux_ssh_sftp_file_open(greedy, "/data/a", O_READ);
    int second = crux_ssh_sftp_file_open(greedy, "/data/b", O_READ);
    CHECK(first >= 0);
    CHECK(second >= 0);
    /* One session cannot take every transfer that the file service has. */
    CHECK(crux_ssh_sftp_file_open(greedy, "/data/c", O_READ) < 0);
    /* A refused write leaves no file behind. */
    CHECK(crux_ssh_sftp_file_open(greedy, "/data/never", O_REPLACE) < 0);
    CHECK(!crux_file_fake_exists(ROOT_DATA, "never"));
    int theirs = crux_ssh_sftp_file_open(other, "/data/c", O_READ);
    CHECK(theirs >= 0);
    /* The pool of the file service is the outer bound, and it gives a plain refusal. */
    void *third = open_as("vera");
    CHECK(crux_ssh_sftp_file_open(third, "/data/a", O_READ) < 0);
    CHECK(crux_ssh_sftp_file_close(greedy, first) == 0);
    int next = crux_ssh_sftp_file_open(third, "/data/a", O_READ);
    CHECK(next >= 0);
    CHECK(crux_ssh_sftp_file_close(third, next) == 0);
    CHECK(crux_ssh_sftp_file_close(other, theirs) == 0);
    CHECK(crux_ssh_sftp_file_close(greedy, second) == 0);

    /* The same for listings of the virtual root, which take nothing from the service. */
    int dirs[8];
    size_t held = 0;
    while (held < 8 && crux_ssh_sftp_dir_open(greedy, "/", &dirs[held]) == 0) {
        held++;
    }
    CHECK(held >= 1 && held < 8);
    for (size_t i = 0; i < held; i++) {
        CHECK(crux_ssh_sftp_dir_close(greedy, dirs[i]) == 0);
    }
}

static void case_a_writer_excludes_others_as_the_service_says(void)
{
    fresh_world();
    place(ROOT_DATA, "doc", "abc");
    void *one = open_as("root");
    void *two = open_as("root");
    int fd = crux_ssh_sftp_file_open(one, "/data/doc", O_WRITE);
    CHECK(fd >= 0);
    CHECK(crux_ssh_sftp_file_open(two, "/data/doc", O_WRITE) < 0);
    CHECK(crux_ssh_sftp_file_open(two, "/data/doc", O_READ) < 0);
    CHECK(crux_ssh_sftp_remove(two, "/data/doc") < 0);
    CHECK(crux_ssh_sftp_rename(two, "/data/doc", "/data/moved") < 0);
    CHECK(crux_ssh_sftp_file_close(one, fd) == 0);
    CHECK(crux_ssh_sftp_remove(two, "/data/doc") == 0);
}

/* ---- Remove, rename, and what has no effect ---- */

static void case_remove_and_rename(void)
{
    fresh_world();
    place(ROOT_DATA, "doc", "abc");
    place(ROOT_DATA, "other", "z");
    place(ROOT_INBOX, "theirs", "z");
    void *fs = open_as("root");

    /* The areas and the root are not files. */
    CHECK(crux_ssh_sftp_remove(fs, "/") < 0);
    CHECK(crux_ssh_sftp_remove(fs, "/data") < 0);
    CHECK(crux_ssh_sftp_remove(fs, "/data/missing") < 0);
    CHECK(crux_ssh_sftp_rename(fs, "/data", "/moved") < 0);
    CHECK(crux_ssh_sftp_rename(fs, "/", "/moved") < 0);
    CHECK(crux_ssh_sftp_rename(fs, "/data/doc", "/data") < 0);
    CHECK(crux_ssh_sftp_rename(fs, "/data/doc", "/") < 0);

    /* Within one area only, and never over an existing name, which WinSCP removes first. */
    CHECK(crux_ssh_sftp_rename(fs, "/data/doc", "/logs/doc") < 0);
    CHECK(crux_ssh_sftp_rename(fs, "/data/doc", "/data/other") < 0);
    CHECK(crux_ssh_sftp_rename(fs, "/data/doc", "/data/doc") < 0);
    CHECK(crux_ssh_sftp_rename(fs, "/data/missing", "/data/x") < 0);
    CHECK(size_on_medium(ROOT_DATA, "doc") == 3);
    CHECK(size_on_medium(ROOT_DATA, "other") == 1);

    CHECK(crux_ssh_sftp_remove(fs, "/data/other") == 0);
    CHECK(crux_ssh_sftp_rename(fs, "/data/doc", "/data/other") == 0);
    CHECK(size_on_medium(ROOT_DATA, "other") == 3);
    CHECK(!crux_file_fake_exists(ROOT_DATA, "doc"));
}

static void case_set_attributes_have_no_effect(void)
{
    fresh_world();
    place(ROOT_DATA, "doc", "abc");
    void *fs = open_as("vera");
    crux_ssh_sftp_attributes_t info = {0};

    CHECK(crux_ssh_sftp_set_attributes(fs, "/data/doc") == 0);
    CHECK(crux_ssh_sftp_set_attributes(fs, "/data") == 0);
    CHECK(crux_ssh_sftp_set_attributes(fs, "/") == 0);
    CHECK(crux_ssh_sftp_attributes(fs, "/data/doc", &info) == 0);
    CHECK(info.mode == (CRUX_SSH_SFTP_MODE_FILE | 0444u));
    CHECK(crux_ssh_sftp_set_attributes(fs, "/missing/doc") < 0);

    int fd = crux_ssh_sftp_file_open(fs, "/data/doc", O_READ);
    CHECK(fd >= 0);
    CHECK(crux_ssh_sftp_file_set_attributes(fs, fd) == 0);
    CHECK(crux_ssh_sftp_file_attributes(fs, fd, &info) == 0);
    CHECK(info.size == 3);
    CHECK(info.mode == (CRUX_SSH_SFTP_MODE_FILE | 0444u));
    CHECK(crux_ssh_sftp_file_close(fs, fd) == 0);
    CHECK(crux_ssh_sftp_file_set_attributes(fs, fd) < 0);
    CHECK(crux_ssh_sftp_file_attributes(fs, fd, &info) < 0);
    CHECK(size_on_medium(ROOT_DATA, "doc") == 3);
}

/* ---- A transfer that stops, and the session that goes away ---- */

static void case_disconnect_keeps_a_partial_upload_as_the_service_says(void)
{
    fresh_world();
    int connection = connect_as("root");
    void *fs = client->file_handle(connection);
    int fd = crux_ssh_sftp_file_open(fs, "/data/big.bin.filepart", O_REPLACE);
    CHECK(fd >= 0);
    CHECK(write_text(fs, fd, "partial", 0) == 7);
    client->disconnect(connection);
    CHECK(!client->is_connected(connection));

    /* The file stays where it is, the name is free, and the transfer came back. */
    CHECK(size_on_medium(ROOT_DATA, "big.bin.filepart") == 7);
    void *again = open_as("root");
    int resumed = crux_ssh_sftp_file_open(again, "/data/big.bin.filepart", O_CREATE);
    CHECK(resumed >= 0);
    crux_ssh_sftp_attributes_t info = {0};
    CHECK(crux_ssh_sftp_file_attributes(again, resumed, &info) == 0);
    CHECK(info.size == 7);
    CHECK(write_text(again, resumed, "-done", 7) == 5);
    CHECK(crux_ssh_sftp_file_close(again, resumed) == 0);
    CHECK(crux_ssh_sftp_rename(again, "/data/big.bin.filepart", "/data/big.bin") == 0);
    CHECK(size_on_medium(ROOT_DATA, "big.bin") == 12);
}

static void case_partial_counts_until_the_stale_rule_needs_its_room(void)
{
    fresh_world();
    static const char block[201] = "0123456789012345678901234567890123456789"
                                   "0123456789012345678901234567890123456789"
                                   "0123456789012345678901234567890123456789"
                                   "0123456789012345678901234567890123456789"
                                   "0123456789012345678901234567890123456789";
    const uint32_t at_start[2] = {0, 0};
    int connection = connect_as("root");
    void *fs = client->file_handle(connection);
    int fd = crux_ssh_sftp_file_open(fs, "/data/a.filepart", O_REPLACE);
    CHECK(fd >= 0);
    CHECK(crux_ssh_sftp_file_pwrite(fs, fd, block, 200, at_start) == 200);
    client->disconnect(connection);

    void *again = open_as("root");
    fd = crux_ssh_sftp_file_open(again, "/data/kept", O_REPLACE);
    CHECK(fd >= 0);
    CHECK(crux_ssh_sftp_file_pwrite(again, fd, block, 200, at_start) == 200);
    CHECK(crux_ssh_sftp_file_close(again, fd) == 0);
    CHECK(crux_file_fake_exists(ROOT_DATA, "a.filepart"));
    CHECK(partial_removals == 0);

    /* The area is full, so the next write takes the stale partial file away. */
    fd = crux_ssh_sftp_file_open(again, "/data/next", O_REPLACE);
    CHECK(fd >= 0);
    CHECK(crux_ssh_sftp_file_pwrite(again, fd, block, 100, at_start) == 100);
    CHECK(crux_ssh_sftp_file_close(again, fd) == 0);
    CHECK(!crux_file_fake_exists(ROOT_DATA, "a.filepart"));
    CHECK(partial_removals == 1);
}

static void case_close_gives_back_all_a_session_holds(void)
{
    fresh_world();
    place(ROOT_DATA, "a", "1");
    int connection = connect_as("root");
    void *fs = client->file_handle(connection);
    int first = crux_ssh_sftp_file_open(fs, "/data/a", O_READ);
    int second = crux_ssh_sftp_file_open(fs, "/data/up", O_REPLACE);
    int dir = -1;
    CHECK(first >= 0);
    CHECK(second >= 0);
    CHECK(crux_ssh_sftp_dir_open(fs, "/data", &dir) == 0);
    client->end_sftp(connection);

    /* All of them came back, so that the whole pool of the service is free. */
    void *one = open_as("root");
    void *two = open_as("root");
    int fds[3];
    fds[0] = crux_ssh_sftp_file_open(one, "/data/a", O_READ);
    fds[1] = crux_ssh_sftp_file_open(one, "/data/up", O_READ);
    fds[2] = crux_ssh_sftp_file_open(two, "/data/a", O_READ);
    for (size_t i = 0; i < 3; i++) {
        CHECK(fds[i] >= 0);
    }
    CHECK(crux_ssh_sftp_dir_open(two, "/data", &dir) == 0);
    CHECK(crux_ssh_sftp_dir_close(two, dir) == 0);
    CHECK(crux_ssh_sftp_file_close(one, fds[0]) == 0);
    CHECK(crux_ssh_sftp_file_close(one, fds[1]) == 0);
    CHECK(crux_ssh_sftp_file_close(two, fds[2]) == 0);
}

static void case_activity_keeps_the_session_and_idleness_ends_it(void)
{
    fresh_world();
    place(ROOT_DATA, "doc", "abcdef");
    int connection = connect_as("root");
    void *fs = client->file_handle(connection);
    int fd = crux_ssh_sftp_file_open(fs, "/data/doc", O_READ);
    CHECK(fd >= 0);
    char buffer[1];

    /* Each request is activity, so a slow transfer outlives the idle limit. */
    for (uint32_t i = 0; i < 4; i++) {
        access_fake_clock_advance(IDLE_LIMIT_MS - 1000u);
        client->pass_read_timeout(connection);
        CHECK(client->is_connected(connection));
        const uint32_t at[2] = {i, 0};
        CHECK(crux_ssh_sftp_file_pread(fs, fd, buffer, sizeof(buffer), at) == 1);
    }
    crux_ssh_sftp_attributes_t info;
    access_fake_clock_advance(IDLE_LIMIT_MS - 1000u);
    client->pass_read_timeout(connection);
    CHECK(client->is_connected(connection));
    CHECK(crux_ssh_sftp_attributes(fs, "/data/doc", &info) == 0);

    /* Then nothing for the idle limit: the provider's next poll ends the connection. */
    access_fake_clock_advance(IDLE_LIMIT_MS + 1000u);
    client->pass_read_timeout(connection);
    CHECK(!client->is_connected(connection));

    /* The handle that it held came back. */
    void *next = open_as("root");
    int fd2 = crux_ssh_sftp_file_open(next, "/data/doc", O_WRITE);
    CHECK(fd2 >= 0);
    CHECK(crux_ssh_sftp_file_close(next, fd2) == 0);
}

static void case_a_session_that_ends_fails_the_next_chunk(void)
{
    fresh_world();
    place(ROOT_DATA, "doc", "abc");
    crux_access_session_t vera = access_test_session("root", ACCESS_TEST_PASSWORD, &access_test_ssh);
    crux_ssh_attachment_t *attachment = crux_ssh_fake_attachment(vera);
    crux_ssh_file_handle_t *handle = NULL;
    CHECK(crux_ssh_sftp_open(NULL, attachment, &handle) == CRUX_OK);
    void *fs = handle;
    int reading = crux_ssh_sftp_file_open(fs, "/data/doc", O_READ);
    int writing = crux_ssh_sftp_file_open(fs, "/data/up", O_REPLACE);
    int dir = -1;
    CHECK(reading >= 0);
    CHECK(writing >= 0);
    CHECK(crux_ssh_sftp_dir_open(fs, "/data", &dir) == 0);
    CHECK(write_text(fs, writing, "abc", 0) == 3);

    crux_access_logout(&vera);

    char buffer[3];
    crux_ssh_sftp_attributes_t info;
    char name[CRUX_SSH_SFTP_NAME_CAPACITY];
    CHECK(crux_ssh_sftp_file_pread(fs, reading, buffer, sizeof(buffer), OFFSET_0) < 0);
    CHECK(write_text(fs, writing, "def", 3) < 0);
    CHECK(crux_ssh_sftp_file_attributes(fs, reading, &info) < 0);
    CHECK(crux_ssh_sftp_attributes(fs, "/", &info) < 0);
    CHECK(crux_ssh_sftp_attributes(fs, "/data/doc", &info) < 0);
    CHECK(crux_ssh_sftp_file_open(fs, "/data/doc", O_READ) < 0);
    CHECK(crux_ssh_sftp_remove(fs, "/data/doc") < 0);
    CHECK(crux_ssh_sftp_set_attributes(fs, "/data/doc") < 0);
    CHECK(crux_ssh_sftp_dir_read(fs, dir, name, sizeof(name), &info) < 0);
    int root = -1;
    CHECK(crux_ssh_sftp_dir_open(fs, "/", &root) < 0);
    /* Closing needs no session. */
    CHECK(crux_ssh_sftp_file_close(fs, reading) == 0);
    CHECK(crux_ssh_sftp_file_close(fs, writing) == 0);
    CHECK(crux_ssh_sftp_dir_close(fs, dir) == 0);
    crux_ssh_sftp_close(NULL, attachment, handle);
    CHECK(size_on_medium(ROOT_DATA, "up") == 3);
}

static void case_a_medium_that_leaves(void)
{
    fresh_world();
    place(ROOT_DATA, "doc", "abc");
    void *fs = open_as("root");
    int reading = crux_ssh_sftp_file_open(fs, "/data/doc", O_READ);
    CHECK(reading >= 0);
    crux_file_fake_remove_medium();
    char buffer[3];
    const uint32_t at[2] = {1, 0};
    CHECK(crux_ssh_sftp_file_pread(fs, reading, buffer, sizeof(buffer), at) < 0);
    crux_ssh_sftp_attributes_t info;
    CHECK(crux_ssh_sftp_attributes(fs, "/data/doc", &info) < 0);
    CHECK(crux_ssh_sftp_file_open(fs, "/data/new", O_REPLACE) < 0);
    /* The handle comes back whatever the close reports. */
    (void)crux_ssh_sftp_file_close(fs, reading);
    CHECK(crux_ssh_sftp_file_close(fs, reading) < 0);
}

static void case_the_start_directory(void)
{
    char small[1];
    char enough[8];
    CHECK(crux_ssh_sftp_start_directory(small, sizeof(small)) == NULL);
    CHECK(crux_ssh_sftp_start_directory(enough, sizeof(enough)) == enough);
    CHECK(strcmp(enough, "/") == 0);
}

static void run_case(const char *name, void (*body)(void))
{
    crux_file_fake_reset();
    CHECK(crux_file_fake_bind_root(ROOT_DATA, "/card/data") == CRUX_FILE_OK);
    CHECK(crux_file_fake_bind_root(ROOT_LOGS, "/card/logs") == CRUX_FILE_OK);
    CHECK(crux_file_fake_bind_root(ROOT_INBOX, "/card/inbox") == CRUX_FILE_OK);
    CHECK(crux_file_fake_bind_root(ROOT_WEB, "/card/web") == CRUX_FILE_OK);
    fflush(stdout);
    pid_t child = fork();
    CHECK(child >= 0);
    if (child == 0) {
        body();
        fflush(stdout);
        _exit(access_test_failures == 0 ? 0 : 1);
    }
    int status = 0;
    CHECK(waitpid(child, &status, 0) == child);
    bool passed = WIFEXITED(status) && WEXITSTATUS(status) == 0;
    printf("%s %s\n", passed ? "PASS" : "FAIL", name);
    CHECK(passed);
}

#define RUN(name) run_case(#name, name)

int main(void)
{
    RUN(case_open_needs_a_live_session);
    RUN(case_session_handles_are_bounded_and_come_back);
    RUN(case_a_handle_works_only_while_it_is_open);
    RUN(case_root_lists_only_visible_areas_with_no_denial);
    RUN(case_area_lists_files_and_hides_what_the_service_hides);
    RUN(case_listing_refusals);
    RUN(case_listing_error_ends_the_listing);
    RUN(case_attributes);
    RUN(case_paths_outside_the_rules_reach_nothing);
    RUN(case_a_refused_name_is_recorded_once_with_no_text);
    RUN(case_open_flags_map_to_the_service_modes);
    RUN(case_open_flags_that_mean_nothing_are_refused);
    RUN(case_read_and_write_together);
    RUN(case_permissions_come_from_the_service);
    RUN(case_a_descriptor_cannot_cross_sessions);
    RUN(case_read_reports_the_end_as_zero);
    RUN(case_write_rules);
    RUN(case_append_writes_at_the_end);
    RUN(case_limits_are_the_services);
    RUN(case_open_files_are_bounded_for_each_session);
    RUN(case_a_writer_excludes_others_as_the_service_says);
    RUN(case_remove_and_rename);
    RUN(case_set_attributes_have_no_effect);
    RUN(case_disconnect_keeps_a_partial_upload_as_the_service_says);
    RUN(case_partial_counts_until_the_stale_rule_needs_its_room);
    RUN(case_close_gives_back_all_a_session_holds);
    RUN(case_activity_keeps_the_session_and_idleness_ends_it);
    RUN(case_a_session_that_ends_fails_the_next_chunk);
    RUN(case_a_medium_that_leaves);
    RUN(case_the_start_directory);
    return access_test_finish("Ssh.sftp");
}
