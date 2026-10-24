/* SPDX-License-Identifier: Apache-2.0 */
/* Exact sample-owned Flash API composition. Original native NOR/controller
 * devices stay deferred; these owner proxies initialize only after private
 * command prerequisites. They do not assert hardware readiness or probe NOR. */
#include "storage_native.h"
#include "configuration_settings.h"
#include "file_native.h"
#include "gate.h"
#include "maintenance.h"
#include "storage_adapter.h"
#include <string.h>
#include <zephyr/device.h>
#include <zephyr/drivers/flash.h>
#include <zephyr/fs/fs.h>
#include <zephyr/fs/littlefs.h>
#include <zephyr/kvss/nvs.h>
#include <zephyr/storage/flash_map.h>

static enum crux_pilot_flash_owner owner(const struct device *dev) {
    return *(const enum crux_pilot_flash_owner *)dev->config;
}
static int read_flash(const struct device *dev, off_t off, void *bytes, size_t len) {
    return crux_candidate_read(owner(dev), off, bytes, len);
}
static int write_flash(const struct device *dev, off_t off, const void *bytes, size_t len) {
    return crux_candidate_write(owner(dev), off, bytes, len);
}
static int erase_flash(const struct device *dev, off_t off, size_t len) {
    return crux_candidate_erase(owner(dev), off, len);
}
static const struct flash_parameters parameters = {.write_block_size = 1, .erase_value = 0xff};
static const struct flash_parameters *get_parameters(const struct device *dev) {
    ARG_UNUSED(dev);
    return &parameters;
}
static const struct flash_pages_layout pages = {.pages_count = 2048, .pages_size = 4096};
static void page_layout(const struct device *dev, const struct flash_pages_layout **layout,
                        size_t *count) {
    ARG_UNUSED(dev);
    *layout = &pages;
    *count = 1;
}
static DEVICE_API(flash, candidate_api) = {.read = read_flash,
                                           .write = write_flash,
                                           .erase = erase_flash,
                                           .get_parameters = get_parameters,
                                           .page_layout = page_layout};
static int init_proxy(const struct device *dev) {
    ARG_UNUSED(dev);
    return crux_candidate_storage_ready() ? 0 : -EACCES;
}
#define OWNER_PROXY(node, value)                                                                   \
    static const enum crux_pilot_flash_owner node##_owner = value;                                 \
    DEVICE_DT_DEFINE(DT_NODELABEL(node), init_proxy, NULL, NULL, &node##_owner, POST_KERNEL, 99,   \
                     &candidate_api)
OWNER_PROXY(candidate_file_backend, CRUX_PILOT_FLASH_FILE);
OWNER_PROXY(candidate_scratch_backend, CRUX_PILOT_FLASH_SCRATCH);
OWNER_PROXY(candidate_nvs_backend, CRUX_PILOT_FLASH_NVS);
static struct flash_area areas[] = {
    {.fa_id = PARTITION_ID(candidate_file),
     .fa_off = PARTITION_OFFSET(candidate_file),
     .fa_size = PARTITION_SIZE(candidate_file),
     .fa_dev = DEVICE_DT_GET(DT_NODELABEL(candidate_file_backend))},
    {.fa_id = PARTITION_ID(candidate_scratch),
     .fa_off = PARTITION_OFFSET(candidate_scratch),
     .fa_size = PARTITION_SIZE(candidate_scratch),
     .fa_dev = DEVICE_DT_GET(DT_NODELABEL(candidate_scratch_backend))},
    {.fa_id = PARTITION_ID(candidate_nvs),
     .fa_off = PARTITION_OFFSET(candidate_nvs),
     .fa_size = PARTITION_SIZE(candidate_nvs),
     .fa_dev = DEVICE_DT_GET(DT_NODELABEL(candidate_nvs_backend))},
};
int crux_candidate_area_open(uint8_t id, const struct flash_area **area) {
    if (!area || !crux_candidate_storage_ready())
        return -EACCES;
    for (size_t i = 0; i < ARRAY_SIZE(areas); ++i)
        if (id == areas[i].fa_id) {
            if (!device_is_ready(areas[i].fa_dev))
                return -ENODEV;
            *area = &areas[i];
            return 0;
        }
    return -EACCES;
}
FS_LITTLEFS_DECLARE_DEFAULT_CONFIG(storage);
static struct fs_mount_t mount = {.type = FS_LITTLEFS,
                                  .fs_data = &storage,
                                  .storage_dev = (void *)PARTITION_ID(candidate_file),
                                  .mnt_point = "/candidate",
                                  .flags = FS_MOUNT_FLAG_NO_FORMAT};
extern int crux_candidate_values_trial(void);
extern int crux_candidate_values_readback(void);
static int scratch_trial(void) {
    const struct flash_area *area;
    uint8_t input[4] = {0x31, 0x42, 0x53, 0x64}, output[4] = {0};
    int rc = crux_candidate_area_open(PARTITION_ID(candidate_scratch), &area);
    if (!rc)
        rc = flash_area_erase(area, 0, 4096);
    if (!rc)
        rc = flash_area_write(area, 0, input, sizeof(input));
    if (!rc)
        rc = flash_area_read(area, 0, output, sizeof(output));
    return rc ? rc : (memcmp(input, output, sizeof(input)) ? -EIO : 0);
}
static int provision_media(void) {
    /* Explicit fresh-media provisioning only. No inferred blank/recovery path. */
    const struct flash_area *area;
    int rc = crux_candidate_area_open(PARTITION_ID(candidate_nvs), &area);
    if (rc)
        return rc;
    rc = flash_erase(area->fa_dev, area->fa_off, area->fa_size);
    struct nvs_fs nvs = {.flash_device = area->fa_dev,
                         .offset = area->fa_off,
                         .sector_size = 4096,
                         .sector_count = 16};
    if (!rc)
        rc = nvs_mount(&nvs);
    uint16_t count = 0x8000;
    if (!rc) {
        ssize_t written = nvs_write(&nvs, 0x8000, &count, sizeof(count));
        rc = written == (ssize_t)sizeof(count) ? 0 : (written < 0 ? (int)written : -EIO);
    }
    if (!rc) {
        maintenance_provisioning(true);
        rc = fs_mkfs(FS_LITTLEFS, (uintptr_t)mount.storage_dev, NULL, 0);
        maintenance_provisioning(false);
    }
    return rc;
}
int crux_candidate_fixed_trial(bool provision) {
    int rc = crux_candidate_storage_begin();
    if (rc)
        return rc;
    bool mounted = false;
    maintenance_newboot();
    gate_reset();
    for (size_t i = 0; !rc && i < ARRAY_SIZE(areas); ++i)
        rc = device_init(areas[i].fa_dev);
    if (!rc)
        rc = scratch_trial();
    if (!rc && provision)
        rc = provision_media();
    if (!rc && crux_config_zephyr_init() != CRUX_CONFIG_OK)
        rc = -EIO;
    if (!rc) {
        rc = fs_mount(&mount);
        mounted = rc == 0;
    }
    if (!rc && crux_file_zephyr_bind(mount.mnt_point) != CRUX_FILE_OK)
        rc = -EIO;
    if (!rc)
        rc = crux_candidate_values_trial();
    /* Unmount/remount while exclusive maintenance still owns all mutations. */
    if (!rc) {
        rc = fs_unmount(&mount);
        mounted = rc != 0;
    }
    if (!rc) {
        rc = fs_mount(&mount);
        mounted = rc == 0;
    }
    if (!rc)
        rc = crux_candidate_values_readback();
    if (mounted) {
        int unmount = fs_unmount(&mount);
        if (!rc)
            rc = unmount;
    }
    bool finished = maintenance_finish();
    bool native_finished = crux_candidate_storage_finish();
    return rc ? rc : (finished && native_finished ? 0 : -EIO);
}
