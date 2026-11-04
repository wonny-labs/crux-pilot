/* SPDX-License-Identifier: Apache-2.0 */
#include "sd_file_ops.h"
#include <crux/file.h>

enum { PROBE = 16 };

static uint8_t pattern_byte(unsigned index)
{
    return (uint8_t)((index * 7u + 3u) & 0xFFu);
}

static enum sd_file_ops_status create_scratch(void)
{
    crux_file_t *file = NULL;
    uint8_t data[SD_FILE_OPS_BYTES];
    size_t count = 0;

    for (unsigned i = 0; i < sizeof(data); i++) {
        data[i] = pattern_byte(i);
    }
    if (crux_file_open(SD_FILE_OPS_NAME, CRUX_FILE_CREATE_REPLACE, &file) != CRUX_FILE_OK) {
        return SD_FILE_OPS_CREATE_FAILED;
    }
    crux_file_result_t rc = crux_file_write(file, data, sizeof(data), &count);
    if (rc == CRUX_FILE_OK && count == sizeof(data)) {
        rc = crux_file_sync(file);
    } else {
        rc = CRUX_FILE_ERR_IO;
    }
    if (crux_file_close(&file) != CRUX_FILE_OK || rc != CRUX_FILE_OK) {
        return SD_FILE_OPS_CREATE_FAILED;
    }
    return SD_FILE_OPS_OK;
}

static enum sd_file_ops_status read_at(crux_file_t *file, unsigned offset)
{
    uint8_t chunk[PROBE];
    size_t count = 0;

    if (crux_file_seek(file, offset) != CRUX_FILE_OK ||
        crux_file_read(file, chunk, sizeof(chunk), &count) != CRUX_FILE_OK) {
        return SD_FILE_OPS_SEEK_FAILED;
    }
    if (count != sizeof(chunk)) {
        return SD_FILE_OPS_SEEK_MISMATCH;
    }
    for (unsigned i = 0; i < sizeof(chunk); i++) {
        if (chunk[i] != pattern_byte(offset + i)) {
            return SD_FILE_OPS_SEEK_MISMATCH;
        }
    }
    return SD_FILE_OPS_OK;
}

static enum sd_file_ops_status check_seek(void)
{
    crux_file_t *file = NULL;
    uint8_t byte;
    size_t count = 1;

    if (crux_file_open(SD_FILE_OPS_NAME, CRUX_FILE_READ, &file) != CRUX_FILE_OK) {
        return SD_FILE_OPS_SEEK_FAILED;
    }
    /* Forward, back to the start, then to the last full chunk, then to the end of the file. */
    enum sd_file_ops_status status = read_at(file, 200);
    if (status == SD_FILE_OPS_OK) {
        status = read_at(file, 0);
    }
    if (status == SD_FILE_OPS_OK) {
        status = read_at(file, SD_FILE_OPS_BYTES - PROBE);
    }
    if (status == SD_FILE_OPS_OK) {
        if (crux_file_seek(file, SD_FILE_OPS_BYTES) != CRUX_FILE_OK ||
            crux_file_read(file, &byte, 1, &count) != CRUX_FILE_OK) {
            status = SD_FILE_OPS_SEEK_FAILED;
        } else if (count != 0) {
            status = SD_FILE_OPS_SEEK_MISMATCH; /* nothing may follow the written bytes */
        }
    }
    if (crux_file_close(&file) != CRUX_FILE_OK && status == SD_FILE_OPS_OK) {
        status = SD_FILE_OPS_SEEK_FAILED;
    }
    return status;
}

enum sd_file_ops_status sd_file_ops_run(void)
{
    enum sd_file_ops_status status = create_scratch();
    if (status != SD_FILE_OPS_OK) {
        return status;
    }
    status = check_seek();
    if (status != SD_FILE_OPS_OK) {
        return status;
    }
    if (crux_file_remove(SD_FILE_OPS_NAME) != CRUX_FILE_OK) {
        return SD_FILE_OPS_REMOVE_FAILED;
    }
    crux_file_t *file = NULL;
    if (crux_file_open(SD_FILE_OPS_NAME, CRUX_FILE_READ, &file) != CRUX_FILE_ERR_NOT_FOUND) {
        if (file != NULL) {
            (void)crux_file_close(&file);
        }
        return SD_FILE_OPS_STILL_PRESENT;
    }
    if (crux_file_remove(SD_FILE_OPS_NAME) != CRUX_FILE_ERR_NOT_FOUND) {
        return SD_FILE_OPS_REMOVE_AGAIN_OK;
    }
    return SD_FILE_OPS_OK;
}
