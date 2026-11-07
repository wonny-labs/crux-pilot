#ifndef CONFIGURATION_FAULT_PROVIDER_H
#define CONFIGURATION_FAULT_PROVIDER_H

#include <crux/configuration.h>

#include <stddef.h>
#include <stdint.h>

/* Private test store, separate from the shared implementation and public usage. */
void config_fault_reset(void);
void config_fault_record(const char *key, const uint8_t *bytes, size_t length);
typedef struct {
    crux_config_result_t result;
    size_t transferred;
} config_fault_read_options_t;
typedef struct {
    crux_config_result_t result;
    bool change_before_error;
} config_fault_write_options_t;
void config_fault_read(config_fault_read_options_t options);
void config_fault_write(config_fault_write_options_t options);
typedef void (*config_fault_hook_t)(void *context);
/* Runs at the start of each write, which a test uses to act while a save holds a lock. */
void config_fault_write_hook(config_fault_hook_t hook, void *context);
/* Fails each write of one key, for a save that stops part of the way through a list. */
void config_fault_write_fail_key(const char *key, crux_config_result_t result);
/* The keys of the writes that were tried, in order, whether or not they succeeded. At most 64. */
size_t config_fault_write_log_count(void);
void config_fault_write_log_clear(void);
const char *config_fault_write_log_key(size_t index);
size_t config_fault_reads(void);
size_t config_fault_writes(void);
size_t config_fault_bytes(const char *key, uint8_t *out, size_t capacity);
#endif
