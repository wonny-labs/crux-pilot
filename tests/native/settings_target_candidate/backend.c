/* SPDX-License-Identifier: Apache-2.0 */
/* Compile actual copied store + NVS backend. NVS/Flash APIs and handler routing are doubles. */
#include "subsys/settings/src/settings_nvs.c"
#include "subsys/settings/src/settings_store.c"
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int reads, deletes, writes, handlers, depth;
static ssize_t name_length = 5, value_length = 8, counter_length = 2;
static int delete_failure, write_failure, mount_failure, fail_write_at;
static bool zero_write, short_value, full_read_error, upper_pair;
static size_t received;
static unsigned char name_bytes[80] = "probe";
static const unsigned char value_bytes[8] = {1, 2, 3, 4, 5, 6, 7, 8};
static uint16_t counter = NVS_NAMECNT_ID + 1;
static const struct device dummy_device = {0};

void settings_lock_take(void) {
    ++depth;
}
void settings_lock_release(void) {
    assert(depth == 1);
    --depth;
}
int settings_commit_subtree(const char *name) {
    (void)name;
    return 0;
}
int settings_name_next(const char *name, const char **next) {
    const char *slash = strchr(name, '/');
    if (next)
        *next = slash ? slash + 1 : NULL;
    return slash ? (int)(slash - name) : (int)strlen(name);
}
int settings_name_steq(const char *name, const char *key, const char **next) {
    size_t n = strlen(key);
    int match = !strncmp(name, key, n) && (name[n] == '\0' || name[n] == '/');
    if (next)
        *next = match ? name + n + (name[n] == '/') : NULL;
    return match;
}
int settings_call_set_handler(const char *name, size_t length, settings_read_cb read, void *context,
                              const struct settings_load_arg *arg) {
    ++handlers;
    const char *key = name;
    if (arg->subtree && !settings_name_steq(name, arg->subtree, &key))
        return 0;
    return arg->cb ? arg->cb(key, length, read, context, arg->param) : 0;
}
static int value_callback(const char *name, size_t length, settings_read_cb read, void *context,
                          void *parameter) {
    (void)name;
    (void)parameter;
    unsigned char buffer[8] = {0};
    if (length != 8)
        return -EBADMSG;
    ssize_t n = read(context, buffer, sizeof(buffer));
    if (n < 0)
        return (int)n;
    received = (size_t)n;
    if (n != 8 || memcmp(buffer, value_bytes, sizeof(buffer)))
        return -EIO;
    return 0;
}
ssize_t nvs_read(struct nvs_fs *fs, uint16_t id, void *buffer, size_t length) {
    (void)fs;
    ++reads;
    const void *bytes;
    ssize_t result;
    if (upper_pair && id == NVS_NAMECNT_ID + 2) {
        memcpy(buffer, "probe", length < 5 ? length : 5);
        return 5;
    }
    if (upper_pair && id == NVS_NAMECNT_ID + 2 + NVS_NAME_ID_OFFSET) {
        memcpy(buffer, value_bytes, length < 8 ? length : 8);
        return 8;
    }
    if (id == NVS_NAMECNT_ID) {
        bytes = &counter;
        result = counter_length;
    } else if (id == NVS_NAMECNT_ID + 1) {
        bytes = name_bytes;
        result = name_length;
    } else if (id == NVS_NAMECNT_ID + 1 + NVS_NAME_ID_OFFSET) {
        bytes = value_bytes;
        result = value_length;
        if (length > 1 && full_read_error)
            return -EIO;
        if (length > 1 && short_value)
            result = 2;
    } else
        return -ENOENT;
    if (result < 0)
        return result;
    size_t n = (size_t)result < length ? (size_t)result : length;
    /* Counter fixture has only two meaningful bytes, even for an oversized record. */
    if (id == NVS_NAMECNT_ID && n > sizeof(counter))
        n = sizeof(counter);
    memcpy(buffer, bytes, n);
    if (id == NVS_NAMECNT_ID + 1 + NVS_NAME_ID_OFFSET && length > 1)
        received = n;
    return result; /* NVS advertises full stored length even with a smaller destination. */
}
ssize_t nvs_write(struct nvs_fs *fs, uint16_t id, const void *bytes, size_t length) {
    (void)fs;
    (void)id;
    (void)bytes;
    ++writes;
    if (write_failure && (!fail_write_at || writes == fail_write_at))
        return write_failure;
    return zero_write ? 0 : (ssize_t)length;
}
int nvs_delete(struct nvs_fs *fs, uint16_t id) {
    (void)fs;
    (void)id;
    ++deletes;
    return deletes == delete_failure ? -EIO : 0;
}
int nvs_mount(struct nvs_fs *fs) {
    (void)fs;
    return mount_failure;
}
int flash_area_open(int id, const struct flash_area **area) {
    (void)id;
    (void)area;
    return -ENOTSUP;
}
int flash_area_get_sectors(int id, uint32_t *count, struct flash_sector *sector) {
    (void)id;
    (void)count;
    (void)sector;
    return -ENOTSUP;
}

int main(int argc, char **argv) {
    if (argc != 2)
        return 2;
    const char *test = argv[1];
    struct settings_nvs cf = {.last_name_id = NVS_NAMECNT_ID + 1, .flash_dev = &dummy_device};
    bool init = !strncmp(test, "init_", 5);
    bool save = !strncmp(test, "save_", 5) || !strncmp(test, "delete_", 7);
    if (strstr(test, "hard_name"))
        name_length = -EIO;
    if (strstr(test, "hard_value"))
        value_length = -EIO;
    if (strstr(test, "orphan_name"))
        value_length = -ENOENT;
    if (strstr(test, "orphan_value"))
        name_length = -ENOENT;
    if (strstr(test, "hole"))
        name_length = value_length = -ENOENT;
    if (strstr(test, "corrupt_value"))
        full_read_error = true;
    if (strstr(test, "short_value"))
        short_value = true;
    if (strstr(test, "oversize_value"))
        value_length = 257;
    if (strstr(test, "bad_name")) {
        memcpy(name_bytes, "pr\0be", 5);
    }
    if (strstr(test, "long_name")) {
        memset(name_bytes, 'x', 80);
        name_length = 75;
    }
    if (strstr(test, "trailing_nul"))
        name_length = 6;
    if (strstr(test, "count_missing"))
        counter_length = -ENOENT;
    if (strstr(test, "count_error"))
        counter_length = -EIO;
    if (strstr(test, "count_short"))
        counter_length = 1;
    if (strstr(test, "count_range"))
        counter = 0xc000;
    if (strstr(test, "mount_error"))
        mount_failure = -EIO;
    if (strstr(test, "zero"))
        zero_write = true;
    if (strstr(test, "new")) {
        cf.last_name_id = NVS_NAMECNT_ID;
        counter = NVS_NAMECNT_ID;
    }
    if (strstr(test, "write_error"))
        write_failure = -EIO;
    if (strstr(test, "name_write_error"))
        fail_write_at = 3;
    if (strstr(test, "value_write_error"))
        fail_write_at = 2;
    if (strstr(test, "delete_error"))
        delete_failure = 1;
    if (strstr(test, "delete_second_error"))
        delete_failure = 2;
    if (strstr(test, "delete_count_error"))
        write_failure = -EIO;
    if (strstr(test, "later_hard_name")) {
        upper_pair = true;
        cf.last_name_id = 32770;
    }
    settings_store_init();
    settings_nvs_src(&cf);
    settings_nvs_dst(&cf);
    int rc;
    if (init)
        rc = settings_nvs_backend_init(&cf);
    else if (save)
        rc = settings_save_one("probe", !strncmp(test, "delete_", 7) ? NULL : value_bytes,
                               !strncmp(test, "delete_", 7) ? 0 : sizeof(value_bytes));
    else
        rc = settings_load_subtree_direct("probe", value_callback, NULL);
    printf("%d %d %d %u %d\n", rc, deletes, writes, cf.last_name_id, depth);
    return 0;
}
