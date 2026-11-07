/*
 * The host key from the credential store, for an image built with CRUX_SSH_TEST_STORED_HOST_KEY.
 * The store runs on its PSA ports over wolfPSA, on FatFs over the flash simulator, whose host file
 * outlives the process, so a test boots the image again on the same file and sees the same key.
 *
 * TEST ONLY. The random numbers come from native_sim's test generator and the device ID from its
 * hwinfo, so a stored key here protects nothing: see docs/psa-crypto-design.md. The image prints
 * the store's events and the fingerprint, and never the key.
 */
#include "credential_port.h"
#include "crux_ssh_access.h"
#include "file_native.h"
#include "test_support.h"

#include <crux_credential_adapter.h>

#include <ff.h>
#include <string.h>
#include <zephyr/fs/fs.h>
#include <zephyr/sys/printk.h>

enum { ROOT_SLOT_A = 1, ROOT_SLOT_B = 2 };
#define MOUNT_POINT "/SD:"
#define DISK_NAME "SD:"
#define PATH_SLOT_A MOUNT_POINT "/secure-a"
#define PATH_SLOT_B MOUNT_POINT "/secure-b"

static FATFS fat;
static struct fs_mount_t mount = {
    .type = FS_FATFS,
    .mnt_point = MOUNT_POINT,
    .fs_data = &fat,
    .flags = FS_MOUNT_FLAG_NO_FORMAT,
};

/* The store's events, which carry no record bytes, for the test on the host to read. */
void crux_credential_port_event(const crux_credential_event_t *event)
{
    static const char *const names[] = {"state",         "slot_invalid", "save_failed",
                                        "key_generated", "key_replaced", "erased"};
    const char *name =
        (size_t)event->type < sizeof(names) / sizeof(names[0]) ? names[event->type] : "unknown";

    printk("STORE event=credential.%s record=%d result=%d state=%d\n", name, (int)event->record,
           (int)event->result, (int)event->state);
}

static bool directory_ready(const char *path)
{
    int result = fs_mkdir(path);

    return result == 0 || result == -EEXIST;
}

/* Formats a blank disk only when the test asks, so that a test can also boot on a disk that does
 * not mount and see the store refuse. */
static bool medium_ready(bool format)
{
    if (fs_mount(&mount) != 0) {
        if (!format || fs_mkfs(FS_FATFS, (uintptr_t)DISK_NAME, NULL, 0) != 0 ||
            fs_mount(&mount) != 0) {
            return false;
        }
    }
    return directory_ready(PATH_SLOT_A) && directory_ready(PATH_SLOT_B) &&
           crux_file_zephyr_bind_root(ROOT_SLOT_A, PATH_SLOT_A) == CRUX_FILE_OK &&
           crux_file_zephyr_bind_root(ROOT_SLOT_B, PATH_SLOT_B) == CRUX_FILE_OK;
}

bool test_stored_host_key_load(crux_ssh_key_type_t type, bool format, test_key_t *key)
{
    crux_ssh_host_key_t host_key;
    crux_credential_result_t store;
    crux_status_t status;

    /* A medium that failed leaves the roots unbound, and the store then reads UNUSABLE. */
    if (!medium_ready(format)) {
        printk("STORE medium not ready\n");
    }
    store = crux_credential_start(ROOT_SLOT_A, ROOT_SLOT_B);
    printk("STORE start=%d state=%d\n", (int)store, (int)crux_credential_state());
    status = crux_ssh_host_key_load(type, &host_key);
    if (status != CRUX_OK) {
        printk("HOST-KEY unavailable status=%d\n", (int)status);
        return false;
    }
    memset(key, 0, sizeof(*key));
    key->type = host_key.type;
    key->host_key_length = host_key.length;
    memcpy(key->host_key, host_key.key, host_key.length);
    crux_ssh_host_key_clear(&host_key);
    return true;
}
