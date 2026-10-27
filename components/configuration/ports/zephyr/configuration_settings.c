/* SPDX-License-Identifier: Apache-2.0 */
#include "configuration_settings.h"
#include "configuration_port.h"
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <zephyr/settings/settings.h>
#include <zephyr/sys/util.h>

#if !defined(CRUX_CONFIGURATION_SETTINGS_SIMULATOR) || !CONFIG_BOARD_NATIVE_SIM ||                 \
    !CONFIG_FLASH_SIMULATOR || !CONFIG_SETTINGS_NVS || !CONFIG_NVS_DATA_CRC
#error "Configuration Settings candidate requires explicit simulator-only composition"
#endif
BUILD_ASSERT(!IS_ENABLED(CONFIG_SETTINGS_NVS_NAME_CACHE));
BUILD_ASSERT(!IS_ENABLED(CONFIG_NVS_INIT_BAD_MEMORY_REGION));

enum { NAME_CAPACITY = 36, RECORD_CAPACITY = 256 };
static bool initialized;

static crux_config_result_t result(int rc)
{
    switch (rc) {
    case 0:
        return CRUX_CONFIG_OK;
    case -ENOSPC:
        return CRUX_CONFIG_ERR_NO_SPACE;
    case -ENODEV:
    case -EAGAIN:
        return CRUX_CONFIG_ERR_NOT_READY;
    case -ENOTSUP:
        return CRUX_CONFIG_ERR_NOT_SUPPORTED;
    default:
        return CRUX_CONFIG_ERR_IO;
    }
}

crux_config_result_t crux_config_zephyr_init(void)
{
    crux_config_result_t rc = result(settings_subsys_init());
    initialized = rc == CRUX_CONFIG_OK;
    return rc;
}

struct read_context {
    uint8_t bytes[RECORD_CAPACITY];
    size_t length;
    bool found;
    int error;
};

static int receive(const char *name, size_t length, settings_read_cb read_cb, void *cb_arg,
                   void *param)
{
    struct read_context *ctx = param;
    if (name != NULL && name[0] != '\0') {
        return 0;
    }
    if (ctx->found || length == 0 || length > sizeof(ctx->bytes)) {
        ctx->error = -EIO;
        return ctx->error;
    }
    ctx->found = true;
    ctx->length = length;
    ssize_t actual = read_cb(cb_arg, ctx->bytes, length);
    if (actual < 0 || (size_t)actual != length) {
        ctx->error = actual < 0 ? (int)actual : -EIO;
    }
    return ctx->error;
}

static bool name_for(const char *key, char name[NAME_CAPACITY])
{
    int length = snprintf(name, NAME_CAPACITY, "cfg/%s", key);
    return length > 0 && length < NAME_CAPACITY;
}

crux_config_result_t crux_config_port_read(const char *key, uint8_t *buffer, size_t capacity,
                                           crux_config_record_info_t *record)
{
    if (!initialized) {
        return CRUX_CONFIG_ERR_NOT_READY;
    }
    char name[NAME_CAPACITY];
    if (!name_for(key, name)) {
        return CRUX_CONFIG_ERR_IO;
    }
    struct read_context ctx = {0};
    int rc = settings_load_subtree_direct(name, receive, &ctx);
    if (ctx.error != 0 || rc != 0) {
        return result(ctx.error != 0 ? ctx.error : rc);
    }
    if (!ctx.found) {
        return CRUX_CONFIG_DEFAULT_MISSING;
    }
    size_t copied = MIN(ctx.length, capacity);
    memcpy(buffer, ctx.bytes, copied);
    record->length = ctx.length;
    record->transferred = copied;
    return CRUX_CONFIG_OK;
}

crux_config_result_t crux_config_port_write(const char *key, const uint8_t *buffer, size_t length)
{
    if (!initialized) {
        return CRUX_CONFIG_ERR_NOT_READY;
    }
    char name[NAME_CAPACITY];
    if (!name_for(key, name)) {
        return CRUX_CONFIG_ERR_IO;
    }
    return result(settings_save_one(name, buffer, length));
}
