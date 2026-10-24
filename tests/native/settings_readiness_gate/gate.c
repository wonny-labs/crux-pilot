/* SPDX-License-Identifier: Apache-2.0 */
/* Private, caller-serialized exclusive-store composition. No production API. */
#include "gate.h"
#include "configuration_port.h"
#include "configuration_settings.h"
#include <crux/configuration.h>
#include <errno.h>
#include <string.h>
#include <zephyr/drivers/flash.h>
#include <zephyr/settings/settings.h>
#include <zephyr/storage/flash_map.h>

BUILD_ASSERT(IS_ENABLED(CONFIG_BOARD_NATIVE_SIM));
BUILD_ASSERT(IS_ENABLED(CONFIG_FLASH_SIMULATOR));
BUILD_ASSERT(!IS_ENABLED(CONFIG_USERSPACE));
enum gate_state { CLOSED, SCANNING, READY, FAULTED, ENDED };
static enum gate_state state;
static const struct device *real;
static struct device proxy;
static struct flash_driver_api api;
static const struct flash_driver_api *original;
static struct flash_area area;
unsigned gate_reads, gate_writes, gate_erases, gate_denied, gate_fail_read;
int gate_write_error;

static bool within(off_t offset, size_t length) {
    return offset >= area.fa_off && (size_t)(offset - area.fa_off) <= area.fa_size &&
           length <= area.fa_size - (size_t)(offset - area.fa_off);
}
static int read_flash(const struct device *dev, off_t offset, void *bytes, size_t length) {
    ARG_UNUSED(dev);
    if (!within(offset, length) || ++gate_reads == gate_fail_read) {
        state = FAULTED;
        return -EIO;
    }
    int rc = original->read(real, offset, bytes, length);
    if (rc != 0)
        state = FAULTED;
    return rc;
}
static bool permit(off_t offset, size_t length) {
    if (state != READY || !within(offset, length)) {
        ++gate_denied;
        state = FAULTED;
        return false;
    }
    return true;
}
static int write_flash(const struct device *dev, off_t offset, const void *bytes, size_t length) {
    ARG_UNUSED(dev);
    if (!permit(offset, length))
        return -EACCES;
    if (gate_write_error) {
        state = FAULTED;
        return gate_write_error;
    }
    ++gate_writes;
    int rc = original->write(real, offset, bytes, length);
    if (rc != 0)
        state = FAULTED;
    return rc;
}
static int erase_flash(const struct device *dev, off_t offset, size_t length) {
    ARG_UNUSED(dev);
    if (!permit(offset, length))
        return -EACCES;
    ++gate_erases;
    int rc = original->erase(real, offset, length);
    if (rc != 0)
        state = FAULTED;
    return rc;
}
bool gate_is_ready(void) {
    return state == READY;
}
bool gate_end_maintenance(void) {
    bool ready = state == READY;
    state = ENDED;
    return ready;
}
void gate_reset(void) {
    state = CLOSED;
    gate_reads = gate_writes = gate_erases = gate_denied = gate_fail_read = 0;
    gate_write_error = 0;
}
extern int __real_flash_area_open(uint8_t id, const struct flash_area **fa);
int __wrap_flash_area_open(uint8_t id, const struct flash_area **fa) {
    int rc = __real_flash_area_open(id, fa);
    if (rc)
        return rc;
    /* Only this chosen Settings partition is supported by the private fixture. */
    if (id != PARTITION_ID(candidate_nvs))
        return 0;
    area = **fa;
    real = area.fa_dev;
    original = real->api;
    proxy = *real;
    api = *original;
    api.read = read_flash;
    api.write = write_flash;
    api.erase = erase_flash;
    proxy.api = &api;
    area.fa_dev = &proxy;
    *fa = &area;
    return 0;
}
/* Do not publish a prefix: every claimed live value is read in full, bounded
 * at this composition boundary. NVS validates CRC only on full-value reads.
 * This checks transport integrity, not application schema/value semantics.
 */
static int receive(const char *name, size_t length, settings_read_cb read_cb, void *cb_arg,
                   void *param) {
    int *error = param;
    uint8_t bytes[256];
    if (*error)
        return *error;
    if (name == NULL || strncmp(name, "cfg/", 4) || !name[4] || strchr(name + 4, '/') ||
        length == 0 || length > sizeof(bytes)) {
        *error = -EBADMSG;
        return *error;
    }
    ssize_t actual = read_cb(cb_arg, bytes, length);
    if (actual < 0 || (size_t)actual != length) {
        *error = actual < 0 ? (int)actual : -EIO;
    }
    return *error;
}
static int scan(void) {
    int error = 0;
    state = SCANNING;
    int rc = settings_load_subtree_direct(NULL, receive, &error);
    if (rc != 0 || error != 0 || state == FAULTED) {
        state = FAULTED;
        return -EIO;
    }
    state = READY;
    return 0;
}
extern crux_config_result_t __real_crux_config_zephyr_init(void);
crux_config_result_t __wrap_crux_config_zephyr_init(void) {
    if (state == FAULTED || state == ENDED)
        return CRUX_CONFIG_ERR_NOT_READY;
    if (state == READY)
        return CRUX_CONFIG_OK;
    state = CLOSED;
    crux_config_result_t rc = __real_crux_config_zephyr_init();
    if (rc != CRUX_CONFIG_OK || state == FAULTED) {
        state = FAULTED;
        return CRUX_CONFIG_ERR_IO;
    }
    return scan() == 0 ? CRUX_CONFIG_OK : CRUX_CONFIG_ERR_IO;
}
extern crux_config_result_t __real_crux_config_port_read(const char *, uint8_t *, size_t,
                                                         crux_config_record_info_t *);
crux_config_result_t __wrap_crux_config_port_read(const char *key, uint8_t *buffer, size_t capacity,
                                                  crux_config_record_info_t *record) {
    if (state != READY)
        return CRUX_CONFIG_ERR_NOT_READY;
    crux_config_result_t rc = __real_crux_config_port_read(key, buffer, capacity, record);
    if (rc != CRUX_CONFIG_OK && rc != CRUX_CONFIG_DEFAULT_MISSING)
        state = FAULTED;
    if (state == FAULTED && (rc == CRUX_CONFIG_OK || rc == CRUX_CONFIG_DEFAULT_MISSING))
        return CRUX_CONFIG_ERR_IO;
    return rc;
}
extern crux_config_result_t __real_crux_config_port_write(const char *, const uint8_t *, size_t);
crux_config_result_t __wrap_crux_config_port_write(const char *key, const uint8_t *buffer,
                                                   size_t length) {
    if (state != READY)
        return CRUX_CONFIG_ERR_NOT_READY;
    if (scan() != 0)
        return CRUX_CONFIG_ERR_IO;
    crux_config_result_t rc = __real_crux_config_port_write(key, buffer, length);
    if (rc != CRUX_CONFIG_OK)
        state = FAULTED;
    if (state == FAULTED && rc == CRUX_CONFIG_OK)
        return CRUX_CONFIG_ERR_IO;
    return rc;
}
