/*
 * SFTP on native_sim, for OpenSSH on the host: the provider, the access adapter and the file port
 * are the real ones, and they reach the real file service on real FatFs over a RAM disk. TEST ONLY:
 * the Access ports and host key are test ones, and it is never flashed or run on a board.
 *
 * Arguments after -testargs: key=<hex of an SSH public key blob>, which every account accepts,
 * idle=<seconds>, and port=<number>.
 */
#include "crux_access_adapter.h"
#include "crux_ssh_callbacks.h"
#include "crux_ssh_wolf_provider.h"
#include "file_native.h"
#include "file_service_port.h"
#include "test_support.h"

#include "cmdline.h"
#include "nsi_main.h"

#include <ff.h>
#include <zephyr/fs/fs.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>
#include <zephyr/sys/sys_heap.h>

#include <stdlib.h>
#include <string.h>

#define KEY_BLOB_MAX 192u
#define ACCOUNT_PASSWORD "correct horse battery"

enum { ROOT_DATA, ROOT_LOGS, ROOT_INBOX, ROOT_WEB };

/* The table of tests/ssh/sftp/openssh/proof_server.c, which the cases expect. */
static const crux_file_area_t areas[] = {
    {"data", ROOT_DATA, "data.read", "data.write", 1024u * 1024u, 3u * 1024u * 1024u, 16u,
     ".filepart", CRUX_FILE_SERVICE_DIRECT},
    {"logs", ROOT_LOGS, "logs.read", NULL, 1024u * 1024u, 3u * 1024u * 1024u, 16u, NULL, 0},
    {"inbox", ROOT_INBOX, "inbox.read", "inbox.write", 1024u * 1024u, 3u * 1024u * 1024u, 16u, NULL,
     CRUX_FILE_SERVICE_DIRECT},
    {"web", ROOT_WEB, "data.read", "data.write", 1024u * 1024u, 3u * 1024u * 1024u, 16u, NULL,
     CRUX_FILE_SERVICE_MANAGED},
};
static const crux_file_service_config_t service_config = {areas, 4, NULL, 0, true};

static const char *const root_permissions[] = {CRUX_ACCESS_PERMISSION_MANAGE, "data.read",
                                               "data.write", "logs.read"};
static const char *const viewer_permissions[] = {"data.read", "logs.read"};
static const char *const dropper_permissions[] = {"inbox.write"};
static const char *const stranger_permissions[] = {"status.read"};
static const char *const auditor_permissions[] = {"data.read", "logs.read", "inbox.read"};
static const crux_access_role_t roles[] = {
    {"administrator", root_permissions, 4}, {"viewer", viewer_permissions, 2},
    {"dropper", dropper_permissions, 1},    {"stranger", stranger_permissions, 1},
    {"auditor", auditor_permissions, 3},
};
static crux_access_policy_t policy = {roles, 5, 900u, 8u * 3600u, NULL};

static const crux_access_origin_t serial = {.channel = CRUX_ACCESS_CHANNEL_SERIAL};

static const char *key_text;
static crux_ssh_config_t config = {.port = 2222,
                                   .max_connections = 4,
                                   .max_auth_attempts = 3,
                                   .auth_timeout_ms = 30000,
                                   .io_timeout_ms = 200};
static test_key_t host_key;
static FATFS fat;
static struct fs_mount_t mount = {
    .type = FS_FATFS,
    .mnt_point = "/RAM:",
    .fs_data = &fat,
    .flags = FS_MOUNT_FLAG_NO_FORMAT,
};

/* In Zephyr's malloc.c, with no header of its own at this revision. */
int malloc_runtime_stats_get(struct sys_memory_stats *stats);

/* The cases read these lines, to see that a rejected name was recorded and its text was not. */
void crux_file_service_port_event(crux_file_service_event_t event, crux_access_session_t session,
                                  const char *area, const char *name, const char *new_name,
                                  bool storage_limit)
{
    ARG_UNUSED(session);
    printk("event %d area=%s name=%s new_name=%s storage_limit=%d\n", (int)event, area,
           name != NULL ? name : "-", new_name != NULL ? new_name : "-", storage_limit ? 1 : 0);
}

static void parse_arguments(void)
{
    int argc = 0;
    char **argv = NULL;

    native_get_test_cmd_line_args(&argc, &argv);
    for (int i = 0; i < argc; ++i) {
        const char *value = strchr(argv[i], '=');

        value = value != NULL ? value + 1 : "";
        if (strncmp(argv[i], "key=", 4) == 0) {
            key_text = value;
        } else if (strncmp(argv[i], "idle=", 5) == 0) {
            policy.session_idle_limit_s = (uint32_t)atoi(value);
        } else if (strncmp(argv[i], "port=", 5) == 0) {
            config.port = (uint16_t)atoi(value);
        }
    }
}

static int hex_value(char digit)
{
    if (digit >= '0' && digit <= '9') {
        return digit - '0';
    }
    return digit >= 'a' && digit <= 'f' ? digit - 'a' + 10 : -1;
}

static size_t decode_hex(const char *text, uint8_t *out, size_t capacity)
{
    size_t length = strlen(text) / 2u;

    if (length > capacity) {
        return 0u;
    }
    for (size_t i = 0; i < length; ++i) {
        int high = hex_value(text[2u * i]);
        int low = hex_value(text[2u * i + 1u]);

        if (high < 0 || low < 0) {
            return 0u;
        }
        out[i] = (uint8_t)(high * 16 + low);
    }
    return length;
}

static bool make_accounts(void)
{
    static const char *const people[][2] = {
        {"vera", "viewer"}, {"dora", "dropper"}, {"stan", "stranger"}, {"audit", "auditor"}};
    static uint8_t blob[KEY_BLOB_MAX];
    const char *password = ACCOUNT_PASSWORD;
    size_t length = strlen(password);
    size_t blob_length = key_text != NULL ? decode_hex(key_text, blob, sizeof(blob)) : 0u;
    crux_access_session_t root = {0};
    uint32_t retry_after_ms = 0;

    if (blob_length == 0u || crux_access_init(&policy) != CRUX_ACCESS_OK ||
        crux_access_provision_first_account("root", password, length, "administrator", &serial) !=
            CRUX_ACCESS_OK ||
        crux_access_login_password("root", password, length, &serial, &root, &retry_after_ms) !=
            CRUX_ACCESS_OK ||
        crux_access_account_add_key(root, "root", blob, blob_length) != CRUX_ACCESS_OK) {
        return false;
    }
    for (size_t i = 0; i < ARRAY_SIZE(people); ++i) {
        if (crux_access_account_create(root, people[i][0], people[i][1], password, length) !=
                CRUX_ACCESS_OK ||
            crux_access_account_add_key(root, people[i][0], blob, blob_length) != CRUX_ACCESS_OK) {
            return false;
        }
    }
    crux_access_logout(&root);
    return true;
}

static bool write_raw(const char *path, const char *text)
{
    struct fs_file_t file;
    bool written;

    fs_file_t_init(&file);
    if (fs_open(&file, path, FS_O_CREATE | FS_O_WRITE) != 0) {
        return false;
    }
    written = fs_write(&file, text, strlen(text)) == (ssize_t)strlen(text);
    return fs_close(&file) == 0 && written;
}

static bool make_medium(void)
{
    static const char *const roots[] = {"/RAM:/data", "/RAM:/logs", "/RAM:/inbox", "/RAM:/web"};
    crux_file_t *file = NULL;
    size_t count = 0;

    if (fs_mkfs(FS_FATFS, (uintptr_t) "RAM:", NULL, 0) != 0 || fs_mount(&mount) != 0) {
        return false;
    }
    for (size_t i = 0; i < ARRAY_SIZE(roots); ++i) {
        if (fs_mkdir(roots[i]) != 0 ||
            crux_file_zephyr_bind_root((crux_file_root_t)i, roots[i]) != CRUX_FILE_OK) {
            return false;
        }
    }
    /* A read-only area gets a file only from outside, and the service counts it at its start. */
    if (crux_file_open_at(ROOT_LOGS, "boot.log", CRUX_FILE_CREATE_REPLACE, &file) != CRUX_FILE_OK ||
        crux_file_write(file, "abc", 3u, &count) != CRUX_FILE_OK ||
        crux_file_close(&file) != CRUX_FILE_OK) {
        return false;
    }
    /* A foreign file, which the provider must skip. File cannot make this, as the name is refused.
     */
    return write_raw("/RAM:/data/from a card", "x");
}

int main(void)
{
    struct sys_memory_stats stats;
    size_t worker = 0;
    size_t listener = 0;
    size_t last_peak = 0;
    size_t last_worker = 0;

    parse_arguments();
    if (!make_medium() || !make_accounts() ||
        crux_file_service_start(&service_config) != CRUX_FILE_SERVICE_OK ||
        !test_key_generate(CRUX_SSH_KEY_ED25519, &host_key)) {
        printk("SETUP failed\n");
        nsi_exit(2);
        return 0;
    }
    config.host_key_type = host_key.type;
    config.host_key = host_key.host_key;
    config.host_key_length = host_key.host_key_length;
    crux_status_t status =
        crux_ssh_wolf_provider()->start(&config, crux_ssh_callback_table());
    if (status != CRUX_OK) {
        printk("START failed %d\n", (int)status);
        nsi_exit(3);
        return 0;
    }
    printk("READY port=%u\n", config.port);
    /* Simulation results only: a 64-bit image with a host stack under each thread. */
    for (;;) {
        k_msleep(2000);
        crux_ssh_wolf_test_stack_used(&worker, &listener);
        if (malloc_runtime_stats_get(&stats) == 0 &&
            (stats.max_allocated_bytes != last_peak || worker != last_worker)) {
            last_peak = stats.max_allocated_bytes;
            last_worker = worker;
            printk("MEASURE heap_peak=%zu heap_now=%zu worker_stack=%zu\n", last_peak,
                   stats.allocated_bytes, worker);
        }
    }
    return 0;
}
