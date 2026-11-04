/* SPDX-License-Identifier: Apache-2.0 */
#include "sd_interrupt.h"
#include <stdio.h>
#include <string.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/sys/crc.h>

enum { MAGIC = 0x31585243u /* "CRX1" */, COUNTER_DIGITS = 9, NAME_BYTES = 16 };

uint8_t sd_interrupt_checksum(const char *text)
{
    uint8_t value = 0;

    for (; *text != '\0'; text++) {
        value = (uint8_t)(value * 31u + (uint8_t)*text);
    }
    return value;
}

void sd_interrupt_make_record(uint32_t seq, uint8_t record[SD_INTERRUPT_RECORD_BYTES])
{
    sys_put_le32(MAGIC, &record[0]);
    sys_put_le32(seq, &record[4]);
    for (uint32_t i = 0; i < SD_INTERRUPT_RECORD_BYTES - 12; i++) {
        record[8 + i] = (uint8_t)((seq * 31u + i * 7u + 5u) & 0xFFu);
    }
    sys_put_le32(crc32_ieee(record, SD_INTERRUPT_RECORD_BYTES - 4),
                 &record[SD_INTERRUPT_RECORD_BYTES - 4]);
}

static bool record_valid(const uint8_t *record, uint32_t seq)
{
    uint8_t expected[SD_INTERRUPT_RECORD_BYTES];

    sd_interrupt_make_record(seq, expected);
    return memcmp(record, expected, sizeof(expected)) == 0;
}

static enum sd_interrupt_status read_counter(uint32_t *value)
{
    crux_file_t *file = NULL;
    char text[COUNTER_DIGITS + 2];
    size_t count = 0;

    *value = 0;
    crux_file_result_t rc = crux_file_open(SD_INTERRUPT_COUNTER, CRUX_FILE_READ, &file);
    if (rc == CRUX_FILE_ERR_NOT_FOUND) {
        return SD_INTERRUPT_OK;
    }
    if (rc != CRUX_FILE_OK) {
        return SD_INTERRUPT_IO;
    }
    rc = crux_file_read(file, text, sizeof(text) - 1, &count);
    if (crux_file_close(&file) != CRUX_FILE_OK || rc != CRUX_FILE_OK) {
        return SD_INTERRUPT_IO;
    }
    if (count == 0 || count > COUNTER_DIGITS) {
        return SD_INTERRUPT_COUNTER_INVALID;
    }
    uint32_t parsed = 0;
    for (size_t i = 0; i < count; i++) {
        if (text[i] < '0' || text[i] > '9') {
            return SD_INTERRUPT_COUNTER_INVALID;
        }
        parsed = parsed * 10u + (uint32_t)(text[i] - '0');
    }
    *value = parsed;
    return SD_INTERRUPT_OK;
}

static enum sd_interrupt_status write_counter(uint32_t value)
{
    crux_file_t *file = NULL;
    char text[COUNTER_DIGITS + 1];
    size_t count = 0;
    int length = snprintf(text, sizeof(text), "%u", (unsigned)value);

    if (length <= 0 || length >= (int)sizeof(text) ||
        crux_file_open(SD_INTERRUPT_COUNTER, CRUX_FILE_CREATE_REPLACE, &file) != CRUX_FILE_OK) {
        return SD_INTERRUPT_IO;
    }
    crux_file_result_t rc = crux_file_write(file, text, (size_t)length, &count);
    rc = (rc == CRUX_FILE_OK && count == (size_t)length) ? crux_file_sync(file) : CRUX_FILE_ERR_IO;
    if (crux_file_close(&file) != CRUX_FILE_OK || rc != CRUX_FILE_OK) {
        return SD_INTERRUPT_IO;
    }
    return SD_INTERRUPT_OK;
}

static void run_name(char name[NAME_BYTES], uint32_t boot)
{
    snprintf(name, NAME_BYTES, "run%u", (unsigned)boot);
}

/* Scan a run file: count the leading valid records, then the bytes that follow them. */
static enum sd_interrupt_status recover(uint32_t boot, struct sd_interrupt_recovery *recovery)
{
    char name[NAME_BYTES];
    crux_file_t *file = NULL;
    uint8_t chunk[SD_INTERRUPT_RECORD_BYTES];

    run_name(name, boot);
    crux_file_result_t rc = crux_file_open(name, CRUX_FILE_READ, &file);
    if (rc == CRUX_FILE_ERR_NOT_FOUND) {
        return SD_INTERRUPT_OK;
    }
    if (rc != CRUX_FILE_OK) {
        return SD_INTERRUPT_IO;
    }
    recovery->previous_run_present = true;
    bool stopped = false;
    for (;;) {
        size_t count = 0;
        rc = crux_file_read(file, chunk, sizeof(chunk), &count);
        if (rc != CRUX_FILE_OK) {
            (void)crux_file_close(&file);
            return SD_INTERRUPT_IO;
        }
        if (count == 0) {
            break;
        }
        if (!stopped && count == sizeof(chunk) && record_valid(chunk, recovery->last_valid + 1u)) {
            recovery->last_valid++;
            continue;
        }
        stopped = true;
        recovery->torn_bytes += (uint32_t)count;
    }
    recovery->damaged_record = recovery->torn_bytes >= SD_INTERRUPT_RECORD_BYTES;
    return crux_file_close(&file) == CRUX_FILE_OK ? SD_INTERRUPT_OK : SD_INTERRUPT_IO;
}

enum sd_interrupt_status sd_interrupt_begin(struct sd_interrupt_recovery *recovery,
                                            crux_file_t **run)
{
    uint32_t counter = 0;

    memset(recovery, 0, sizeof(*recovery));
    *run = NULL;
    enum sd_interrupt_status status = read_counter(&counter);
    if (status != SD_INTERRUPT_OK) {
        return status;
    }
    status = write_counter(counter + 1u);
    if (status != SD_INTERRUPT_OK) {
        return status;
    }
    recovery->boot = counter + 1u;
    if (recovery->boot > 1u) {
        status = recover(recovery->boot - 1u, recovery);
        if (status != SD_INTERRUPT_OK) {
            return status;
        }
    }
    char name[NAME_BYTES];
    run_name(name, recovery->boot);
    if (crux_file_open(name, CRUX_FILE_CREATE_REPLACE, run) != CRUX_FILE_OK) {
        *run = NULL;
        return SD_INTERRUPT_IO;
    }
    return SD_INTERRUPT_OK;
}

enum sd_interrupt_status sd_interrupt_append(crux_file_t *run, uint32_t seq)
{
    uint8_t record[SD_INTERRUPT_RECORD_BYTES];
    size_t count = 0;

    sd_interrupt_make_record(seq, record);
    crux_file_result_t rc = crux_file_write(run, record, sizeof(record), &count);
    if (rc != CRUX_FILE_OK || count != sizeof(record)) {
        return SD_INTERRUPT_IO;
    }
    return crux_file_sync(run) == CRUX_FILE_OK ? SD_INTERRUPT_OK : SD_INTERRUPT_IO;
}
