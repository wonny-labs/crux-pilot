/**
 * @file
 * @brief Application-owned scalar and bounded string configuration, with explicit persistence.
 *
 * Thread context only. Callers serialise access. Changes to accepted values are
 * not automatically applied to running services or saved to storage.
 */
#ifndef CRUX_CONFIGURATION_H
#define CRUX_CONFIGURATION_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Longest string value in bytes, excluding the terminator. A build that changes it sets
 * CRUX_CONFIG_STRING_MAX in CMake, which makes it a public compile definition of the target, so
 * that every user of this header agrees. A parameter's storage cannot hold more than this.
 */
#ifndef CRUX_CONFIG_STRING_MAX
#define CRUX_CONFIG_STRING_MAX 128
#endif

typedef enum {
    CRUX_CONFIG_BOOL = 1,
    CRUX_CONFIG_U32 = 2,
    CRUX_CONFIG_STRING = 3
} crux_config_type_t;

/** Tagged value. Use the union member selected by type. */
typedef struct {
    crux_config_type_t type;
    union {
        bool boolean;
        uint32_t u32;
        /**
         * NUL-terminated at length, with no earlier NUL, and well-formed UTF-8 with no control
         * characters (see crux_config_text_valid). A value read from a parameter borrows the
         * parameter's storage, so it stays valid only until the next update or load. A C++17
         * consumer sets this at run time, because bool stays the first union member.
         */
        struct {
            const char *text;
            size_t length; /**< Excludes the terminator. */
        } string;
    } data;
} crux_config_value_t;

/**
 * Synchronous, non-reentrant validator.
 * Must not perform persistence or mutate the parameter being validated.
 */
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
    char *storage;
    size_t capacity;
} crux_config_param_t;

/**
 * Configuration-specific results; never cast to crux_status_t.
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

/**
 * Initialise with a validated default; no persistent read is performed.
 * @param parameter Caller-zeroed storage, unchanged on failed init.
 * @param spec Immutable descriptor kept alive by the caller. A string spec is an invalid argument:
 * use crux_config_init_string().
 * @return CRUX_CONFIG_OK or a CRUX_CONFIG_ERR_* result.
 */
crux_config_result_t crux_config_init(crux_config_param_t *parameter,
                                      const crux_config_spec_t *spec);

/**
 * Initialise a string parameter with a validated default; no persistent read is performed.
 * @param parameter Caller-zeroed storage, unchanged on failed init.
 * @param spec Immutable descriptor of type CRUX_CONFIG_STRING kept alive by the caller, with its
 * default's text. Any other type is an invalid argument.
 * @param storage Holds the accepted value and outlives the parameter. It must not overlap the
 * default.
 * @param capacity Size of storage, above the default's length and at most
 * CRUX_CONFIG_STRING_MAX + 1. Longer values are rejected.
 * @retval CRUX_CONFIG_OK Initialised.
 * @retval CRUX_CONFIG_ERR_INVALID_ARGUMENT A missing argument, a bad key or type, a capacity
 * outside those bounds, a default with no text, or a default that overlaps storage.
 * @retval CRUX_CONFIG_ERR_INVALID_STATE The parameter is already initialised; it is unchanged.
 * @retval CRUX_CONFIG_ERR_REJECTED The default fails the character rule, its length does not land
 * on its terminator, or the validator refuses it.
 */
crux_config_result_t crux_config_init_string(crux_config_param_t *parameter,
                                             const crux_config_spec_t *spec, char *storage,
                                             size_t capacity);

/**
 * The key of an initialised parameter, for a service that holds only the object.
 * @param parameter Any parameter.
 * @return The key, borrowed from the spec, or NULL if parameter is NULL or not initialised.
 */
const char *crux_config_key(const crux_config_param_t *parameter);

/**
 * The character rule that every string value meets: well-formed UTF-8, with no NUL, C0 control, DEL
 * or C1 control. Reads exactly length bytes, and needs no terminator.
 *
 * It keeps terminal escapes and line breaks out of what a command line or a log later shows. It
 * does not make text safe to display: U+2028, bidirectional controls and similar characters pass,
 * so an adapter still escapes for its own format. Other management services use the same rule.
 *
 * @param text Bytes to check. NULL fails.
 * @param length Number of bytes.
 * @return True if the bytes meet the rule.
 */
bool crux_config_text_valid(const char *text, size_t length);

/**
 * Read the currently accepted value, without accessing storage.
 * @param parameter Initialised parameter.
 * @param[out] out Assigned on success; preserved on error. For a string, data.string.text borrows
 * the parameter's storage until the next update or load, unlike a scalar's copy.
 * @return CRUX_CONFIG_OK or a CRUX_CONFIG_ERR_* result.
 */
crux_config_result_t crux_config_read(const crux_config_param_t *parameter,
                                      crux_config_value_t *out);

/**
 * Validate and update the accepted value in memory only.
 * @param parameter Initialised parameter.
 * @param candidate New typed value; rejected updates preserve the old value. A string's text is
 * checked against the character rule and the parameter's capacity before the validator sees it,
 * then copied. It may alias the parameter's storage.
 * @return CRUX_CONFIG_OK or a CRUX_CONFIG_ERR_* result.
 */
crux_config_result_t crux_config_update(crux_config_param_t *parameter,
                                        const crux_config_value_t *candidate);

/**
 * Load one parameter, falling back only on confirmed missing or invalid records.
 * @param parameter Initialised parameter; unresolved IO failure preserves its value.
 * @return CRUX_CONFIG_OK for a valid stored value; DEFAULT_* for a validated
 * fallback; otherwise a CRUX_CONFIG_ERR_* result.
 */
crux_config_result_t crux_config_load(crux_config_param_t *parameter);

/**
 * Validate and save one parameter; no multi-parameter atomicity is promised.
 * @param parameter Initialised parameter.
 * @return CRUX_CONFIG_OK on provider acknowledgement or a CRUX_CONFIG_ERR_* result.
 * A failed save may already have changed persistent bytes.
 */
crux_config_result_t crux_config_save(const crux_config_param_t *parameter);

#ifdef __cplusplus
}
#endif

#endif
