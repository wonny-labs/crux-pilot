/* SPDX-License-Identifier: Apache-2.0 */
#include "sd_replace.h"
#include "sd_storage.h"
#include <stdio.h>
#include <string.h>
#include <zephyr/fs/fs.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/sys/crc.h>

#define CFG SD_STORAGE_SETTINGS_DIR "/cfg"
#define CMP SD_STORAGE_SETTINGS_DIR "/cfg.cmp"
#define COUNTER SD_STORAGE_SETTINGS_DIR "/repcount"

enum { MAGIC = 0x32585243u /* "CRX2" */, COUNTER_DIGITS = 9 };

void sd_replace_make_record(uint32_t gen, uint32_t index, uint8_t record[SD_REPLACE_RECORD_BYTES])
{
    sys_put_le32(MAGIC, &record[0]);
    sys_put_le32(gen, &record[4]);
    sys_put_le32(index, &record[8]);
    for (uint32_t i = 0; i < 16; i++) {
        record[12 + i] = (uint8_t)((gen * 31u + index * 17u + i * 7u + 3u) & 0xFFu);
    }
    sys_put_le32(crc32_ieee(record, SD_REPLACE_RECORD_BYTES - 4),
                 &record[SD_REPLACE_RECORD_BYTES - 4]);
}

enum sd_replace_status sd_replace_write(const char *path, uint32_t gen)
{
    struct fs_file_t file;
    uint8_t record[SD_REPLACE_RECORD_BYTES];

    fs_file_t_init(&file);
    if (fs_open(&file, path, FS_O_CREATE | FS_O_WRITE | FS_O_TRUNC) != 0) {
        return SD_REPLACE_IO;
    }
    int rc = 0;
    for (uint32_t index = 0; index < SD_REPLACE_RECORDS && rc == 0; index++) {
        sd_replace_make_record(gen, index, record);
        rc = fs_write(&file, record, sizeof(record)) == (ssize_t)sizeof(record) ? 0 : -1;
    }
    if (rc == 0) {
        rc = fs_sync(&file);
    }
    int closed = fs_close(&file);
    return (rc == 0 && closed == 0) ? SD_REPLACE_OK : SD_REPLACE_IO;
}

enum sd_replace_status sd_replace_generation(uint32_t gen)
{
    enum sd_replace_status status = sd_replace_write(CMP, gen);

    if (status != SD_REPLACE_OK) {
        return status;
    }
    /* The same fs_rename the Settings File backend ends its compaction with: FatFs removes the
     * destination inside it. */
    return fs_rename(CMP, CFG) == 0 ? SD_REPLACE_OK : SD_REPLACE_IO;
}

static void classify(const char *path, struct sd_replace_file *out)
{
    struct fs_dirent entry;
    struct fs_file_t file;
    uint8_t record[SD_REPLACE_RECORD_BYTES];

    out->state = SD_REPLACE_NONE;
    out->gen = 0;
    if (fs_stat(path, &entry) != 0) {
        return;
    }
    out->state = SD_REPLACE_BAD;
    fs_file_t_init(&file);
    if (fs_open(&file, path, FS_O_READ) != 0) {
        return;
    }
    bool valid = entry.size == (size_t)SD_REPLACE_RECORDS * SD_REPLACE_RECORD_BYTES;
    for (uint32_t index = 0; index < SD_REPLACE_RECORDS; index++) {
        if (fs_read(&file, record, sizeof(record)) != (ssize_t)sizeof(record)) {
            valid = false;
            break;
        }
        if (index == 0 && sys_get_le32(&record[0]) == MAGIC) {
            out->gen = sys_get_le32(&record[4]);
        }
        uint8_t expected[SD_REPLACE_RECORD_BYTES];
        sd_replace_make_record(out->gen, index, expected);
        if (memcmp(record, expected, sizeof(expected)) != 0) {
            valid = false;
        }
    }
    (void)fs_close(&file);
    if (valid) {
        out->state = SD_REPLACE_VALID;
    }
}

static enum sd_replace_status read_counter(uint32_t *value)
{
    struct fs_dirent entry;
    struct fs_file_t file;
    char text[COUNTER_DIGITS + 2];

    *value = 0;
    if (fs_stat(COUNTER, &entry) != 0) {
        return SD_REPLACE_OK;
    }
    fs_file_t_init(&file);
    if (fs_open(&file, COUNTER, FS_O_READ) != 0) {
        return SD_REPLACE_IO;
    }
    ssize_t count = fs_read(&file, text, sizeof(text) - 1);
    if (fs_close(&file) != 0 || count < 0) {
        return SD_REPLACE_IO;
    }
    if (count == 0 || count > COUNTER_DIGITS) {
        return SD_REPLACE_COUNTER_INVALID;
    }
    uint32_t parsed = 0;
    for (ssize_t i = 0; i < count; i++) {
        if (text[i] < '0' || text[i] > '9') {
            return SD_REPLACE_COUNTER_INVALID;
        }
        parsed = parsed * 10u + (uint32_t)(text[i] - '0');
    }
    *value = parsed;
    return SD_REPLACE_OK;
}

static enum sd_replace_status write_counter(uint32_t value)
{
    struct fs_file_t file;
    char text[COUNTER_DIGITS + 1];
    int length = snprintf(text, sizeof(text), "%u", (unsigned)value);

    fs_file_t_init(&file);
    if (length <= 0 || length >= (int)sizeof(text) ||
        fs_open(&file, COUNTER, FS_O_CREATE | FS_O_WRITE | FS_O_TRUNC) != 0) {
        return SD_REPLACE_IO;
    }
    int rc = fs_write(&file, text, (size_t)length) == length ? 0 : -1;
    if (rc == 0) {
        rc = fs_sync(&file);
    }
    int closed = fs_close(&file);
    return (rc == 0 && closed == 0) ? SD_REPLACE_OK : SD_REPLACE_IO;
}

enum sd_replace_status sd_replace_begin(struct sd_replace_recovery *recovery)
{
    uint32_t counter = 0;

    memset(recovery, 0, sizeof(*recovery));
    enum sd_replace_status status = read_counter(&counter);
    if (status != SD_REPLACE_OK) {
        return status;
    }
    status = write_counter(counter + 1u);
    if (status != SD_REPLACE_OK) {
        return status;
    }
    recovery->boot = counter + 1u;
    classify(CFG, &recovery->cfg);
    classify(CMP, &recovery->cmp);
    return SD_REPLACE_OK;
}
