#ifndef CRUX_CONFIGURATION_H
#define CRUX_CONFIGURATION_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum { CRUX_CONFIG_BOOL = 1, CRUX_CONFIG_U32 = 2 } crux_config_type_t;
typedef struct {
    crux_config_type_t type;
    union {
        bool boolean;
        uint32_t u32;
    } data;
} crux_config_value_t;

typedef bool (*crux_config_validator_t)(const crux_config_value_t *candidate, void *context);
typedef struct {
    const char *key;
    uint16_t schema_version;
    crux_config_value_t default_value;
    crux_config_validator_t validate;
    void *validation_context;
} crux_config_spec_t;

/* Caller-zeroed storage. Fields are reserved after init; do not copy live objects. */
typedef struct {
    const crux_config_spec_t *spec;
    crux_config_value_t value;
} crux_config_param_t;

/* Separate from crux_status_t: compare named results; never cast result domains.
 * Positive values are explicit fallback outcomes, not errors or stored success. */
typedef enum {
    CRUX_CONFIG_OK = 0,
    CRUX_CONFIG_DEFAULT_MISSING = 1,
    CRUX_CONFIG_DEFAULT_REJECTED = 2,
    CRUX_CONFIG_ERR_INVALID_ARGUMENT = -1,
    CRUX_CONFIG_ERR_INVALID_STATE = -2,
    CRUX_CONFIG_ERR_REJECTED = -3,
    CRUX_CONFIG_ERR_NOT_READY = -4,
    CRUX_CONFIG_ERR_IO = -5,
    CRUX_CONFIG_ERR_NO_SPACE = -6,
    CRUX_CONFIG_ERR_NOT_SUPPORTED = -7
} crux_config_result_t;

/* Task context, caller-serialized. Immutable spec/key and context outlive object.
 * Init/read/update do not access persistence. Load publishes only validated data
 * or revalidated defaults; transport failure keeps the accepted value. Save
 * revalidates, acknowledges one parameter only, and failure does not mean no
 * stored bytes changed. Application owns cross-parameter/runtime activation.
 * See docs/configuration-contract.md for exact encoding, limits and lifetime. */
crux_config_result_t crux_config_init(crux_config_param_t *parameter,
                                      const crux_config_spec_t *spec);
crux_config_result_t crux_config_read(const crux_config_param_t *parameter,
                                      crux_config_value_t *out);
crux_config_result_t crux_config_update(crux_config_param_t *parameter,
                                        const crux_config_value_t *candidate);
crux_config_result_t crux_config_load(crux_config_param_t *parameter);
crux_config_result_t crux_config_save(const crux_config_param_t *parameter);

#ifdef __cplusplus
}
#endif
#endif
