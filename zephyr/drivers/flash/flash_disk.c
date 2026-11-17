/* SPDX-License-Identifier: Apache-2.0 */
/*
 * A flash-API device over a disk_access disk. Prototype: it lets a Zephyr flash map, and so
 * MCUboot's port, read and write image slots in the user area of an eMMC. See
 * docs/boot-emmc-backend-prototype.md for what it is for and what is decided and what is not.
 *
 * What the trailer replay of docs/evidence/boot-emmc/trailer-replay shows in its models, and this
 * driver does about it:
 *  - Erase is emulated by writing 0xFF over whole pages, and the device reports an erase value of
 *    0xFF. A medium that reads 0x00 when erased would otherwise make MCUboot's trailer read as bad.
 *  - Every write and erase call is flushed before it returns, one flush for the call. A multi-page
 *    erase goes upwards and is flushed once after its first chunk as well, so that the page with
 *    an image header is erased on the medium before the pages that hold the trailer. A write or
 *    erase that is not flushed may be lost or reordered by the device cache, and MCUboot orders
 *    its trailer writes by assuming that a returned write is durable.
 *  - A failed command, or a failed flush, fails the call with -EIO, and a failed command is not
 *    followed by a flush, so a call is acknowledged only when its data is on the medium. A call
 *    that fails part way leaves its range undefined: the chunks before the failure may be on the
 *    medium, or held in the cache until a later flush. A caller treats the range as unwritten.
 *  - A write block is one 512-byte sector, and offsets and lengths that are not whole write
 *    blocks, or leave the device, are refused before any command is issued.
 *  - Partitions must start and end on a page of the layout, checked at build time.
 *
 * Nothing here is evidence about an eMMC device: the tests run it on native_sim against a model.
 */
#define DT_DRV_COMPAT crux_flash_disk

#include <errno.h>
#include <stdint.h>
#include <string.h>
#include <zephyr/device.h>
#include <zephyr/drivers/disk.h>
#include <zephyr/drivers/flash.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/storage/disk_access.h>
#include <zephyr/sys/util.h>

LOG_MODULE_REGISTER(crux_flash_disk, CONFIG_FLASH_LOG_LEVEL);

#define SECTOR_SIZE 512U
#define ERASE_VALUE 0xFFU
#define BUFFER_BYTES ((size_t)CONFIG_CRUX_FLASH_DISK_BUFFER_SECTORS * SECTOR_SIZE)

#ifdef CONFIG_SDHC_BUFFER_ALIGNMENT
#define BUFFER_ALIGN MAX(CONFIG_SDHC_BUFFER_ALIGNMENT, 32)
#else
#define BUFFER_ALIGN 32
#endif

struct flash_disk_config {
    const char *disk_name;
    uint32_t page_size;
    const uint64_t *partition_ends; /* byte offset of the end of each partition, from devicetree */
    size_t partition_count;
};

struct flash_disk_data {
    struct k_mutex lock;
    uint32_t sector_count;
#if defined(CONFIG_FLASH_PAGE_LAYOUT)
    struct flash_pages_layout layout;
#endif
    uint8_t buffer[BUFFER_BYTES] __aligned(BUFFER_ALIGN);
};

static const struct flash_parameters flash_disk_parameters = {
    .write_block_size = SECTOR_SIZE,
    .erase_value = ERASE_VALUE,
};

static uint64_t device_size(const struct flash_disk_data *data)
{
    return (uint64_t)data->sector_count * SECTOR_SIZE;
}

/* Overflow-safe: offset + len is never computed. */
static bool in_range(const struct flash_disk_data *data, off_t offset, size_t len)
{
    uint64_t size = device_size(data);

    return offset >= 0 && (uint64_t)offset <= size && (uint64_t)len <= size - (uint64_t)offset;
}

static int flush(const struct flash_disk_config *config)
{
    if (IS_ENABLED(CONFIG_CRUX_FLASH_DISK_FLUSH_CONTROL)) {
        return 0;
    }
    int rc = disk_access_ioctl(config->disk_name, DISK_IOCTL_CTRL_SYNC, NULL);

    if (rc != 0) {
        LOG_ERR("%s: the cache flush failed (%d)", config->disk_name, rc);
        return -EIO;
    }
    return 0;
}

static int flash_disk_read(const struct device *dev, off_t offset, void *out, size_t len)
{
    const struct flash_disk_config *config = dev->config;
    struct flash_disk_data *data = dev->data;
    uint8_t *dest = out;
    uint64_t pos = (uint64_t)offset; /* off_t may be 32 bits on a target: never advance it */
    int rc = 0;

    if (!in_range(data, offset, len)) {
        return -EINVAL;
    }
    if (len == 0U) {
        return 0;
    }

    k_mutex_lock(&data->lock, K_FOREVER);
    while (len > 0U) {
        uint32_t sector = (uint32_t)(pos / SECTOR_SIZE);
        size_t head = (size_t)(pos % SECTOR_SIZE);
        size_t chunk;

        if (head == 0U && len >= SECTOR_SIZE && IS_ALIGNED((uintptr_t)dest, BUFFER_ALIGN)) {
            size_t sectors = MIN(len / SECTOR_SIZE, CONFIG_CRUX_FLASH_DISK_DIRECT_SECTORS);

            int read_rc = disk_access_read(config->disk_name, dest, sector, sectors);

            if (read_rc != 0) {
                LOG_ERR("%s: a read command failed at sector %u (%d)", config->disk_name,
                        (unsigned int)sector, read_rc);
                rc = -EIO;
                break;
            }
            chunk = sectors * SECTOR_SIZE;
        } else {
            size_t sectors = MIN(DIV_ROUND_UP(head + len, SECTOR_SIZE),
                                 (size_t)CONFIG_CRUX_FLASH_DISK_BUFFER_SECTORS);

            int read_rc = disk_access_read(config->disk_name, data->buffer, sector, sectors);

            if (read_rc != 0) {
                LOG_ERR("%s: a read command failed at sector %u (%d)", config->disk_name,
                        (unsigned int)sector, read_rc);
                rc = -EIO;
                break;
            }
            chunk = MIN(len, sectors * SECTOR_SIZE - head);
            memcpy(dest, &data->buffer[head], chunk);
        }
        dest += chunk;
        pos += chunk;
        len -= chunk;
    }
    k_mutex_unlock(&data->lock);
    return rc;
}

/* Writes `len` bytes at a sector offset, from `source`, or the erase value if it is NULL. The
 * caller holds the lock and has checked the range and the alignment. With `flush_first`, the cache
 * is flushed after the first chunk when more follow: an erase goes upwards from the start, so that
 * the first page, which holds an image header, is erased on the medium before the pages that hold
 * its trailer. */
static int write_sectors(const struct flash_disk_config *config, struct flash_disk_data *data,
                         uint64_t offset, const uint8_t *source, size_t len, bool flush_first)
{
    bool first = true;

    while (len > 0U) {
        size_t chunk = MIN(len, BUFFER_BYTES);

        if (source != NULL) {
            memcpy(data->buffer, source, chunk);
            source += chunk;
        } else {
            memset(data->buffer, ERASE_VALUE, chunk);
        }
        int write_rc =
            disk_access_write(config->disk_name, data->buffer, (uint32_t)(offset / SECTOR_SIZE),
                              (uint32_t)(chunk / SECTOR_SIZE));

        if (write_rc != 0) {
            LOG_ERR("%s: a write command failed at offset %lld (%d)", config->disk_name,
                    (long long)offset, write_rc);
            return -EIO;
        }
        offset += chunk;
        len -= chunk;
        if (first && flush_first && len > 0U && flush(config) != 0) {
            return -EIO;
        }
        first = false;
    }
    return 0;
}

static int flash_disk_write(const struct device *dev, off_t offset, const void *source, size_t len)
{
    const struct flash_disk_config *config = dev->config;
    struct flash_disk_data *data = dev->data;
    int rc;

    if (IS_ENABLED(CONFIG_CRUX_FLASH_DISK_READ_ONLY)) {
        return -ENOTSUP;
    }
    if (source == NULL || len == 0U || !in_range(data, offset, len) ||
        ((uint64_t)offset % SECTOR_SIZE) != 0U || (len % SECTOR_SIZE) != 0U) {
        return -EINVAL;
    }

    k_mutex_lock(&data->lock, K_FOREVER);
    rc = write_sectors(config, data, (uint64_t)offset, source, len, false);
    if (rc == 0) {
        rc = flush(config);
    }
    k_mutex_unlock(&data->lock);
    return rc;
}

static int flash_disk_erase(const struct device *dev, off_t offset, size_t size)
{
    const struct flash_disk_config *config = dev->config;
    struct flash_disk_data *data = dev->data;
    int rc;

    if (IS_ENABLED(CONFIG_CRUX_FLASH_DISK_READ_ONLY)) {
        return -ENOTSUP;
    }
    if (size == 0U || !in_range(data, offset, size) ||
        ((uint64_t)offset % config->page_size) != 0U || (size % config->page_size) != 0U) {
        return -EINVAL;
    }

    k_mutex_lock(&data->lock, K_FOREVER);
    rc = write_sectors(config, data, (uint64_t)offset, NULL, size, true);
    if (rc == 0) {
        rc = flush(config);
    }
    k_mutex_unlock(&data->lock);
    return rc;
}

static const struct flash_parameters *flash_disk_get_parameters(const struct device *dev)
{
    ARG_UNUSED(dev);
    return &flash_disk_parameters;
}

static int flash_disk_get_size(const struct device *dev, uint64_t *size)
{
    *size = device_size(dev->data);
    return 0;
}

#if defined(CONFIG_FLASH_PAGE_LAYOUT)
static void flash_disk_page_layout(const struct device *dev,
                                   const struct flash_pages_layout **layout, size_t *layout_size)
{
    struct flash_disk_data *data = dev->data;

    *layout = &data->layout;
    *layout_size = 1;
}
#endif

static int flash_disk_init(const struct device *dev)
{
    const struct flash_disk_config *config = dev->config;
    struct flash_disk_data *data = dev->data;
    uint32_t sector_size = 0;
    uint32_t sector_count = 0;
    int rc;

    k_mutex_init(&data->lock);
    data->sector_count = 0; /* every failure below leaves a device that refuses all access */

    rc = disk_access_init(config->disk_name);
    if (rc != 0) {
        LOG_ERR("%s: the disk did not initialise (%d)", config->disk_name, rc);
        return rc;
    }
    if (disk_access_ioctl(config->disk_name, DISK_IOCTL_GET_SECTOR_COUNT, &sector_count) != 0 ||
        disk_access_ioctl(config->disk_name, DISK_IOCTL_GET_SECTOR_SIZE, &sector_size) != 0) {
        LOG_ERR("%s: the disk geometry is not available", config->disk_name);
        return -EIO;
    }
    if (sector_size != SECTOR_SIZE || sector_count == 0U) {
        LOG_ERR("%s: a sector of %u bytes and %u sectors are not supported", config->disk_name,
                (unsigned int)sector_size, (unsigned int)sector_count);
        return -ENOTSUP;
    }
    for (size_t i = 0; i < config->partition_count; i++) {
        if (config->partition_ends[i] > (uint64_t)sector_count * SECTOR_SIZE) {
            LOG_ERR("%s: a partition ends beyond the disk", config->disk_name);
            return -EINVAL;
        }
    }
    data->sector_count = sector_count;
#if defined(CONFIG_FLASH_PAGE_LAYOUT)
    data->layout.pages_size = config->page_size;
    data->layout.pages_count = (size_t)(device_size(data) / config->page_size);
#endif
    return 0;
}

static DEVICE_API(flash, flash_disk_api) = {
    .read = flash_disk_read,
    .write = flash_disk_write,
    .erase = flash_disk_erase,
    .get_parameters = flash_disk_get_parameters,
    .get_size = flash_disk_get_size,
#if defined(CONFIG_FLASH_PAGE_LAYOUT)
    .page_layout = flash_disk_page_layout,
#endif
};

/* A partition starts and ends on a page, so that no erase of one slot reaches the next. */
#define PARTITION_ON_PAGES(node_id, page)                                                          \
    BUILD_ASSERT(DT_REG_ADDR(node_id) % (page) == 0 && DT_REG_SIZE(node_id) % (page) == 0,         \
                 "partition " DT_NODE_PATH(node_id) " is not on pages of erase-block-size");

#define FLASH_DISK_PARTITIONS(n)                                                                   \
    COND_CODE_1(                                                                                   \
        DT_NODE_EXISTS(DT_INST_CHILD(n, partitions)),                                              \
        (DT_FOREACH_CHILD_STATUS_OKAY_VARGS(DT_INST_CHILD(n, partitions), PARTITION_ON_PAGES,      \
                                            DT_INST_PROP(n, erase_block_size))),                   \
        ())

#define PARTITION_END(node_id) ((uint64_t)DT_REG_ADDR(node_id) + DT_REG_SIZE(node_id))

#define FLASH_DISK_PARTITION_ENDS(n)                                                               \
    COND_CODE_1(                                                                                   \
        DT_NODE_EXISTS(DT_INST_CHILD(n, partitions)),                                              \
        (DT_FOREACH_CHILD_STATUS_OKAY_SEP(DT_INST_CHILD(n, partitions), PARTITION_END, (, ))),     \
        (0))

#define FLASH_DISK_INIT(n)                                                                         \
    BUILD_ASSERT(DT_INST_PROP(n, erase_block_size) > 0, "erase-block-size must not be zero");      \
    BUILD_ASSERT(DT_INST_PROP(n, write_block_size) == SECTOR_SIZE,                                 \
                 "only a write block of one sector is supported");                                 \
    BUILD_ASSERT(DT_INST_PROP(n, erase_block_size) % SECTOR_SIZE == 0,                             \
                 "erase-block-size must be a multiple of the sector");                             \
    FLASH_DISK_PARTITIONS(n)                                                                       \
    static const uint64_t flash_disk_partition_ends_##n[] = {FLASH_DISK_PARTITION_ENDS(n)};        \
    static const struct flash_disk_config flash_disk_config_##n = {                                \
        .disk_name = DT_INST_PROP(n, disk_name),                                                   \
        .page_size = DT_INST_PROP(n, erase_block_size),                                            \
        .partition_ends = flash_disk_partition_ends_##n,                                           \
        .partition_count = ARRAY_SIZE(flash_disk_partition_ends_##n),                              \
    };                                                                                             \
    static struct flash_disk_data flash_disk_data_##n;                                             \
    DEVICE_DT_INST_DEFINE(n, flash_disk_init, NULL, &flash_disk_data_##n, &flash_disk_config_##n,  \
                          POST_KERNEL, CONFIG_CRUX_FLASH_DISK_INIT_PRIORITY, &flash_disk_api);

DT_INST_FOREACH_STATUS_OKAY(FLASH_DISK_INIT)
