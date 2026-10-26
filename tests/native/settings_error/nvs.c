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
static int delete_failure, write_failure, mount_failure;
static bool zero_write, short_value, full_read_error;
static size_t received;
static unsigned char name_bytes[80] = "probe";
static const unsigned char value_bytes[8] = {1, 2, 3, 4, 5, 6, 7, 8};
static uint16_t counter = NVS_NAMECNT_ID + 1;
static const struct device dummy_device = {0};

void settings_lock_take(void)
{
    ++depth;
}
void settings_lock_release(void)
{
    assert(depth == 1);
    --depth;
}
int settings_commit_subtree(const char *name)
{
    (void)name;
    return 0;
}
int settings_name_next(const char *name, const char **next)
{
    const char *slash = strchr(name, '/');
    if (next)
        *next = slash ? slash + 1 : NULL;
    return slash ? (int)(slash - name) : (int)strlen(name);
}
int settings_name_steq(const char *name, const char *key, const char **next)
{
    size_t n = strlen(key);
    int match = !strncmp(name, key, n) && (name[n] == '\0' || name[n] == '/');
    if (next)
        *next = match ? name + n + (name[n] == '/') : NULL;
    return match;
}
int settings_call_set_handler(const char *name, size_t length, settings_read_cb read, void *context,
                              const struct settings_load_arg *arg)
{
    ++handlers;
    const char *key = name;
    if (arg->subtree && !settings_name_steq(name, arg->subtree, &key))
        return 0;
    return arg->cb ? arg->cb(key, length, read, context, arg->param) : 0;
}
static int value_callback(const char *name, size_t length, settings_read_cb read, void *context,
                          void *parameter)
{
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
ssize_t nvs_read(struct nvs_fs *fs, uint16_t id, void *buffer, size_t length)
{
    (void)fs;
    ++reads;
    const void *bytes;
    ssize_t result;
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
ssize_t nvs_write(struct nvs_fs *fs, uint16_t id, const void *bytes, size_t length)
{
    (void)fs;
    (void)id;
    (void)bytes;
    ++writes;
    if (write_failure)
        return write_failure;
    return zero_write ? 0 : (ssize_t)length;
}
int nvs_delete(struct nvs_fs *fs, uint16_t id)
{
    (void)fs;
    (void)id;
    ++deletes;
    return deletes == delete_failure ? -EIO : 0;
}
int nvs_mount(struct nvs_fs *fs)
{
    (void)fs;
    return mount_failure;
}
int flash_area_open(int id, const struct flash_area **area)
{
    (void)id;
    (void)area;
    return -ENOTSUP;
}
int flash_area_get_sectors(int id, uint32_t *count, struct flash_sector *sector)
{
    (void)id;
    (void)count;
    (void)sector;
    return -ENOTSUP;
}

int main(int argc, char **argv)
{
    if (argc != 2)
        return 2;
    const char *test = argv[1];
    struct settings_nvs cf = {.last_name_id = NVS_NAMECNT_ID + 1, .flash_dev = &dummy_device};
    if (strstr(test, "hard_name") || strstr(test, "save_error"))
        name_length = -EIO;
    if (!strcmp(test, "hard_value"))
        value_length = -EIO;
    if (strstr(test, "both_missing") || strstr(test, "count_error") || !strcmp(test, "repair_zero"))
        name_length = value_length = -ENOENT;
    if (strstr(test, "orphan") || strstr(test, "delete_"))
        value_length = -ENOENT;
    if (!strcmp(test, "delete_first"))
        delete_failure = 1;
    if (!strcmp(test, "delete_second"))
        delete_failure = 2;
    if (!strcmp(test, "count_error") || !strcmp(test, "integrated_count_error"))
        write_failure = -EIO;
    if (!strcmp(test, "save_value_error"))
        write_failure = -EIO;
    if (!strcmp(test, "repair_zero") || !strcmp(test, "save_zero"))
        zero_write = true;
    if (strstr(test, "long_name")) {
        memset(name_bytes, 'x', sizeof(name_bytes));
        name_length = 75;
    }
    if (!strcmp(test, "boundary_name")) {
        memset(name_bytes, 'x', sizeof(name_bytes));
        name_length = 74;
    }
    if (!strcmp(test, "maximum_name")) {
        memset(name_bytes, 'x', sizeof(name_bytes));
        name_length = 73;
    }
    if (strstr(test, "embedded")) {
        memcpy(name_bytes, "probe\0evil", 10);
        name_length = 10;
    }
    if (!strcmp(test, "trailing_nul"))
        name_length = 6;
    if (!strcmp(test, "empty_name"))
        name_length = 0;
    if (!strcmp(test, "integrated_short"))
        short_value = true;
    if (!strcmp(test, "value_read_error"))
        full_read_error = true;
#if CONFIG_SETTINGS_NVS_NAME_CACHE
    if (!strcmp(test, "save_cache_complete"))
        cf.loaded = true;
    if (!strcmp(test, "save_cache_overflow")) {
        cf.loaded = true;
        cf.cache_total = 3;
    }
    if (!strcmp(test, "cached_missing"))
        name_length = -ENOENT;
    if (strstr(test, "cached_")) {
        cf.cache[0].name_hash = crc16_ccitt(0xffff, "probe", 5);
        cf.cache[0].name_id = NVS_NAMECNT_ID + 1;
    }
#endif
    ssize_t result;
    if (!strncmp(test, "init_", 5)) {
        if (!strcmp(test, "init_missing"))
            counter_length = -ENOENT;
        if (!strcmp(test, "init_read_error"))
            counter_length = -EIO;
        if (!strcmp(test, "init_short"))
            counter_length = 1;
        if (!strcmp(test, "init_long"))
            counter_length = 3;
        if (!strcmp(test, "init_low"))
            counter = NVS_NAMECNT_ID - 1;
        if (!strcmp(test, "init_high"))
            counter = NVS_NAMECNT_ID + NVS_NAME_ID_OFFSET;
        if (!strcmp(test, "init_mount_error"))
            mount_failure = -EIO;
        if (!strcmp(test, "init_no_device"))
            cf.flash_dev = NULL;
        result = settings_nvs_backend_init(&cf);
    } else if (strstr(test, "save_") || strstr(test, "cached_")) {
        result = settings_nvs_save(&cf.cf_store, "probe", (const char *)value_bytes,
                                   sizeof(value_bytes));
    } else if (!strncmp(test, "integrated_", 11)) {
        settings_store_init();
        settings_nvs_src(&cf);
        unsigned char buffer[8] = {0};
        size_t capacity = !strcmp(test, "integrated_small") ? 2 : sizeof(buffer);
        result = settings_load_one("probe", buffer, capacity);
        if (received && memcmp(buffer, value_bytes, received))
            return 3;
    } else {
        struct settings_load_arg arg = {.cb = value_callback};
        result = settings_nvs_load(&cf.cf_store, &arg);
    }
    int loaded = -1;
#if CONFIG_SETTINGS_NVS_NAME_CACHE
    loaded = cf.loaded;
#endif
    printf("%zd %d %d %d %d %u %d %zu %d\n", result, reads, deletes, writes, handlers,
           cf.last_name_id, loaded, received, depth);
    return 0;
}
