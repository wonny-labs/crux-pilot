/** @file
 * @brief Application-owned scalar configuration and explicit persistence.
 *
 * Thread context only. Callers serialise access. Changes to accepted values are
 * not automatically applied to running services or saved to storage.
 */
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

/** Synchronous validator. Must not re-enter persistence or mutate its parameter. */
typedef bool (*crux_config_validator_t)(const crux_config_value_t *candidate, void *context);

/** Caller-owned descriptor; key and validation context must outlive the parameter. */
typedef struct {
    const char *key;
    uint16_t schema_version;
    crux_config_value_t default_value;
    crux_config_validator_t validate;
    void *validation_context;
} crux_config_spec_t;

/** Caller-zeroed storage; fields are reserved after init. Do not copy live objects. */
typedef struct {
    const crux_config_spec_t *spec;
    crux_config_value_t value;
} crux_config_param_t;

/** Configuration-specific results; never cast to crux_status_t.
 * Positive DEFAULT_* values mean a validated default was used, not a storage write.
 */
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

/** Initialise with a validated default; no persistent read is performed.
 * @param parameter Caller-zeroed storage, unchanged on failed init.
 * @param spec Immutable descriptor kept alive by the caller.
 * @return CRUX_CONFIG_OK or a CRUX_CONFIG_ERR_* result.
 */
crux_config_result_t crux_config_init(crux_config_param_t *parameter,
                                      const crux_config_spec_t *spec);

/** Read the currently accepted value, without accessing storage.
 * @param parameter Initialised parameter.
 * @param[out] out Assigned on success; preserved on error.
 * @return CRUX_CONFIG_OK or a CRUX_CONFIG_ERR_* result.
 */
crux_config_result_t crux_config_read(const crux_config_param_t *parameter,
                                      crux_config_value_t *out);

/** Validate and update the accepted value in memory only.
 * @param parameter Initialised parameter.
 * @param candidate New typed value; rejected updates preserve the old value.
 * @return CRUX_CONFIG_OK or a CRUX_CONFIG_ERR_* result.
 */
crux_config_result_t crux_config_update(crux_config_param_t *parameter,
                                        const crux_config_value_t *candidate);

/** Load one parameter, falling back only on confirmed missing or invalid records.
 * @param parameter Initialised parameter; unresolved IO failure preserves its value.
 * @return CRUX_CONFIG_OK for a valid stored value; DEFAULT_* for a validated
 * fallback; otherwise a CRUX_CONFIG_ERR_* result.
 */
crux_config_result_t crux_config_load(crux_config_param_t *parameter);

/** Validate and save one parameter; no multi-parameter atomicity is promised.
 * @param parameter Initialised parameter.
 * @return CRUX_CONFIG_OK on provider acknowledgement or a CRUX_CONFIG_ERR_* result.
 * A failed save may already have changed persistent bytes.
 */
crux_config_result_t crux_config_save(const crux_config_param_t *parameter);

#ifdef __cplusplus
}
#endif

#endif
