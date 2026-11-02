/* SPDX-License-Identifier: Apache-2.0 */
#include "sd_file_check.h"
#include <crux/file.h>
#include <stdio.h>
#include <string.h>

enum { CHUNK = 512, COUNTER_DIGITS = 9 };
_Static_assert(SD_FILE_CHECK_PATTERN_BYTES % CHUNK == 0,
               "the pattern is written and verified in whole chunks");

static uint8_t pattern_byte(uint32_t index)
{
    return (uint8_t)((index * 7u + 13u) & 0xFFu);
}

static enum sd_file_check_status read_counter(uint32_t *value)
{
    crux_file_t *file = NULL;
    char text[COUNTER_DIGITS + 2];
    size_t count = 0;

    *value = 0;
    crux_file_result_t rc = crux_file_open(SD_FILE_CHECK_COUNTER, CRUX_FILE_READ, &file);
    if (rc == CRUX_FILE_ERR_NOT_FOUND) {
        return SD_FILE_CHECK_OK;
    }
    if (rc != CRUX_FILE_OK) {
        return SD_FILE_CHECK_COUNTER_IO;
    }
    rc = crux_file_read(file, text, sizeof(text) - 1, &count);
    if (crux_file_close(&file) != CRUX_FILE_OK || rc != CRUX_FILE_OK) {
        return SD_FILE_CHECK_COUNTER_IO;
    }
    if (count == 0 || count > COUNTER_DIGITS) {
        return SD_FILE_CHECK_COUNTER_INVALID;
    }
    uint32_t parsed = 0;
    for (size_t i = 0; i < count; i++) {
        if (text[i] < '0' || text[i] > '9') {
            return SD_FILE_CHECK_COUNTER_INVALID;
        }
        parsed = parsed * 10u + (uint32_t)(text[i] - '0');
    }
    *value = parsed;
    return SD_FILE_CHECK_OK;
}

static enum sd_file_check_status write_counter(uint32_t value)
{
    crux_file_t *file = NULL;
    char text[COUNTER_DIGITS + 1];
    size_t count = 0;
    int length = snprintf(text, sizeof(text), "%u", (unsigned)value);

    if (length <= 0 || length >= (int)sizeof(text)) {
        return SD_FILE_CHECK_COUNTER_IO;
    }
    if (crux_file_open(SD_FILE_CHECK_COUNTER, CRUX_FILE_CREATE_REPLACE, &file) != CRUX_FILE_OK) {
        return SD_FILE_CHECK_COUNTER_IO;
    }
    crux_file_result_t rc = crux_file_write(file, text, (size_t)length, &count);
    if (rc == CRUX_FILE_OK && count == (size_t)length) {
        rc = crux_file_sync(file);
    } else {
        rc = CRUX_FILE_ERR_IO;
    }
    if (crux_file_close(&file) != CRUX_FILE_OK || rc != CRUX_FILE_OK) {
        return SD_FILE_CHECK_COUNTER_IO;
    }
    return SD_FILE_CHECK_OK;
}

static enum sd_file_check_status create_pattern(void)
{
    crux_file_t *file = NULL;
    uint8_t chunk[CHUNK];

    if (crux_file_open(SD_FILE_CHECK_PATTERN, CRUX_FILE_CREATE_REPLACE, &file) != CRUX_FILE_OK) {
        return SD_FILE_CHECK_PATTERN_IO;
    }
    crux_file_result_t rc = CRUX_FILE_OK;
    for (uint32_t offset = 0; offset < SD_FILE_CHECK_PATTERN_BYTES && rc == CRUX_FILE_OK;
         offset += CHUNK) {
        size_t count = 0;
        for (uint32_t i = 0; i < CHUNK; i++) {
            chunk[i] = pattern_byte(offset + i);
        }
        rc = crux_file_write(file, chunk, CHUNK, &count);
        if (rc == CRUX_FILE_OK && count != CHUNK) {
            rc = CRUX_FILE_ERR_IO;
        }
    }
    if (rc == CRUX_FILE_OK) {
        rc = crux_file_sync(file);
    }
    if (crux_file_close(&file) != CRUX_FILE_OK || rc != CRUX_FILE_OK) {
        return SD_FILE_CHECK_PATTERN_IO;
    }
    return SD_FILE_CHECK_OK;
}

/* Missing is reported through *missing; any other open failure is an I/O error. */
static enum sd_file_check_status verify_pattern(bool *missing)
{
    crux_file_t *file = NULL;
    uint8_t chunk[CHUNK];

    *missing = false;
    crux_file_result_t rc = crux_file_open(SD_FILE_CHECK_PATTERN, CRUX_FILE_READ, &file);
    if (rc == CRUX_FILE_ERR_NOT_FOUND) {
        *missing = true;
        return SD_FILE_CHECK_OK;
    }
    if (rc != CRUX_FILE_OK) {
        return SD_FILE_CHECK_PATTERN_IO;
    }
    enum sd_file_check_status status = SD_FILE_CHECK_OK;
    for (uint32_t offset = 0; offset < SD_FILE_CHECK_PATTERN_BYTES; offset += CHUNK) {
        size_t count = 0;
        rc = crux_file_read(file, chunk, CHUNK, &count);
        if (rc != CRUX_FILE_OK) {
            status = SD_FILE_CHECK_PATTERN_IO;
            break;
        }
        if (count != CHUNK) {
            status = SD_FILE_CHECK_PATTERN_MISMATCH;
            break;
        }
        for (uint32_t i = 0; i < CHUNK; i++) {
            if (chunk[i] != pattern_byte(offset + i)) {
                status = SD_FILE_CHECK_PATTERN_MISMATCH;
                break;
            }
        }
        if (status != SD_FILE_CHECK_OK) {
            break;
        }
    }
    if (status == SD_FILE_CHECK_OK) {
        size_t count = 0;
        rc = crux_file_read(file, chunk, 1, &count); /* nothing may follow the pattern */
        if (rc != CRUX_FILE_OK) {
            status = SD_FILE_CHECK_PATTERN_IO;
        } else if (count != 0) {
            status = SD_FILE_CHECK_PATTERN_MISMATCH;
        }
    }
    if (crux_file_close(&file) != CRUX_FILE_OK && status == SD_FILE_CHECK_OK) {
        status = SD_FILE_CHECK_PATTERN_IO;
    }
    return status;
}

enum sd_file_check_status sd_file_check_run(struct sd_file_check *check)
{
    uint32_t counter = 0;

    memset(check, 0, sizeof(*check));
    enum sd_file_check_status status = read_counter(&counter);
    if (status != SD_FILE_CHECK_OK) {
        return status;
    }
    status = write_counter(counter + 1u);
    if (status != SD_FILE_CHECK_OK) {
        return status;
    }
    check->boot_count = counter + 1u;

    bool missing = false;
    status = verify_pattern(&missing);
    if (status != SD_FILE_CHECK_OK || !missing) {
        return status;
    }
    status = create_pattern();
    if (status != SD_FILE_CHECK_OK) {
        return status;
    }
    check->pattern_created = true;
    return verify_pattern(&missing);
}
