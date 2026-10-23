#include "fault_provider.h"
#include "configuration_port.h"
#include <string.h>

enum { RECORDS = 4, KEY_BYTES = 32, RECORD_BYTES = 16 };
static struct {
    char key[KEY_BYTES];
    uint8_t bytes[RECORD_BYTES];
    size_t length;
    int present;
} records[RECORDS];
static crux_config_result_t read_result, write_result;
static size_t read_limit, reads, writes;
static int change_first;

static size_t find(const char *key) {
    for (size_t i = 0; i < RECORDS; ++i) {
        if (records[i].present && strcmp(records[i].key, key) == 0) {
            return i;
        }
    }
    return RECORDS;
}
void config_fault_reset(void) {
    memset(records, 0, sizeof(records));
    read_result = write_result = CRUX_CONFIG_OK;
    read_limit = SIZE_MAX;
    reads = writes = 0;
    change_first = 0;
}
void config_fault_record(const char *key, const uint8_t *bytes, size_t length) {
    size_t slot = find(key);
    if (slot == RECORDS) {
        for (slot = 0; slot < RECORDS && records[slot].present; ++slot) {
        }
    }
    if (slot == RECORDS || strlen(key) >= KEY_BYTES || length > RECORD_BYTES) {
        return;
    }
    memcpy(records[slot].key, key, strlen(key) + 1);
    memcpy(records[slot].bytes, bytes, length);
    records[slot].length = length;
    records[slot].present = 1;
}
void config_fault_read(config_fault_read_options_t options) {
    read_result = options.result;
    read_limit = options.transferred;
}
void config_fault_write(config_fault_write_options_t options) {
    write_result = options.result;
    change_first = options.change_before_error;
}
size_t config_fault_reads(void) {
    return reads;
}
size_t config_fault_writes(void) {
    return writes;
}
size_t config_fault_bytes(const char *key, uint8_t *out, size_t capacity) {
    size_t slot = find(key);
    if (slot == RECORDS) {
        return 0;
    }
    size_t copied = records[slot].length < capacity ? records[slot].length : capacity;
    memcpy(out, records[slot].bytes, copied);
    return records[slot].length;
}
crux_config_result_t crux_config_port_read(const char *key, uint8_t *buffer, size_t capacity,
                                           crux_config_record_info_t *record) {
    ++reads;
    if (read_result != CRUX_CONFIG_OK) {
        return read_result;
    }
    size_t slot = find(key);
    if (slot == RECORDS) {
        return CRUX_CONFIG_DEFAULT_MISSING;
    }
    record->length = records[slot].length;
    size_t copied = record->length < capacity ? record->length : capacity;
    if (copied > read_limit) {
        copied = read_limit;
    }
    memcpy(buffer, records[slot].bytes, copied);
    record->transferred = copied;
    return CRUX_CONFIG_OK;
}
crux_config_result_t crux_config_port_write(const char *key, const uint8_t *buffer, size_t length) {
    ++writes;
    if (write_result == CRUX_CONFIG_OK || change_first) {
        config_fault_record(key, buffer, length);
    }
    return write_result;
}
