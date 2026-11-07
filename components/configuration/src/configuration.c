#include "configuration_port.h"

#include <crux/configuration.h>

#include <string.h>

_Static_assert(CRUX_CONFIG_STRING_MAX >= 1, "A string parameter needs room for a value");

enum {
    KEY_LIMIT = 31,
    HEADER_LENGTH = 4,
    /* The largest record, and a terminator for the validator to see. */
    WIRE_MAX = HEADER_LENGTH + CRUX_CONFIG_STRING_MAX + 1,
    BOOL_LENGTH = 5,
    U32_LENGTH = 8
};

static bool valid_scalar(crux_config_type_t type)
{
    return type == CRUX_CONFIG_BOOL || type == CRUX_CONFIG_U32;
}

/* The wire buffer can hold a sensitive value. A volatile write cannot be removed as a dead store.
 */
static void clear_wire(uint8_t *wire, size_t size)
{
    volatile uint8_t *bytes = wire;
    for (size_t i = 0; i < size; ++i) {
        bytes[i] = 0;
    }
}

static bool valid_key(const char *key)
{
    if (key == NULL) {
        return false;
    }

    size_t length = 0;
    for (; length <= KEY_LIMIT; ++length) {
        unsigned char c = (unsigned char)key[length];
        if (c == 0) {
            break;
        }
        if (length == KEY_LIMIT || !((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                                     (c >= '0' && c <= '9') || c == '_' || c == '-' || c == '.')) {
            return false;
        }
    }
    return length > 0 && !(length == 1 && key[0] == '.') &&
           !(length == 2 && key[0] == '.' && key[1] == '.');
}

/* The first NUL must be the one at length, found without reading past it. Length is bounded first,
 * so a wrong one can neither wrap nor send the search beyond the caller's text. */
static bool string_well_formed(const crux_config_value_t *value, size_t capacity)
{
    const char *text = value->data.string.text;
    size_t length = value->data.string.length;
    return text != NULL && length < capacity && memchr(text, '\0', length + 1) == text + length &&
           crux_config_text_valid(text, length);
}

static bool accepted(const crux_config_spec_t *spec, size_t capacity,
                     const crux_config_value_t *value)
{
    if (value->type != spec->default_value.type) {
        return false;
    }
    if (value->type == CRUX_CONFIG_STRING ? !string_well_formed(value, capacity)
                                          : !valid_scalar(value->type)) {
        return false;
    }
    return spec->validate == NULL || spec->validate(value, spec->validation_context);
}

static void publish(crux_config_param_t *parameter, const crux_config_value_t *value)
{
    if (value->type != CRUX_CONFIG_STRING) {
        parameter->value = *value;
        return;
    }
    size_t length = value->data.string.length;
    /* A candidate may alias the storage, including a tail of the current value. */
    memmove(parameter->storage, value->data.string.text, length + 1);
    parameter->value.type = CRUX_CONFIG_STRING;
    parameter->value.data.string.text = parameter->storage;
    parameter->value.data.string.length = length;
}

static crux_config_result_t ready(const crux_config_param_t *parameter)
{
    if (parameter == NULL) {
        return CRUX_CONFIG_ERR_INVALID_ARGUMENT;
    }
    return parameter->spec == NULL ? CRUX_CONFIG_ERR_INVALID_STATE : CRUX_CONFIG_OK;
}

static crux_config_result_t port_error(crux_config_result_t result)
{
    switch (result) {
    case CRUX_CONFIG_ERR_NOT_READY:
    case CRUX_CONFIG_ERR_IO:
    case CRUX_CONFIG_ERR_NO_SPACE:
    case CRUX_CONFIG_ERR_NOT_SUPPORTED:
        return result;
    default:
        return CRUX_CONFIG_ERR_IO;
    }
}

static crux_config_result_t fallback(crux_config_param_t *parameter, crux_config_result_t outcome)
{
    crux_config_value_t value = parameter->spec->default_value;
    if (!accepted(parameter->spec, parameter->capacity, &value)) {
        return CRUX_CONFIG_ERR_REJECTED;
    }
    publish(parameter, &value);
    return outcome;
}

crux_config_result_t crux_config_init(crux_config_param_t *parameter,
                                      const crux_config_spec_t *spec)
{
    if (parameter == NULL || spec == NULL) {
        return CRUX_CONFIG_ERR_INVALID_ARGUMENT;
    }
    if (parameter->spec != NULL) {
        return CRUX_CONFIG_ERR_INVALID_STATE;
    }

    if (!valid_key(spec->key) || !valid_scalar(spec->default_value.type)) {
        return CRUX_CONFIG_ERR_INVALID_ARGUMENT;
    }
    crux_config_value_t value = spec->default_value;
    if (!accepted(spec, 0, &value)) {
        return CRUX_CONFIG_ERR_REJECTED;
    }

    parameter->value = value;
    parameter->spec = spec;
    return CRUX_CONFIG_OK;
}

/* Whether the bytes of a default, with its terminator, overlap the storage. */
static bool overlaps(const char *text, size_t length, const char *storage, size_t capacity)
{
    uintptr_t text_start = (uintptr_t)text;
    uintptr_t storage_start = (uintptr_t)storage;
    return text_start < storage_start + capacity && storage_start < text_start + length + 1;
}

crux_config_result_t crux_config_init_string(crux_config_param_t *parameter,
                                             const crux_config_spec_t *spec, char *storage,
                                             size_t capacity)
{
    if (parameter == NULL || spec == NULL || storage == NULL) {
        return CRUX_CONFIG_ERR_INVALID_ARGUMENT;
    }
    if (parameter->spec != NULL) {
        return CRUX_CONFIG_ERR_INVALID_STATE;
    }

    const crux_config_value_t *value = &spec->default_value;
    if (!valid_key(spec->key) || value->type != CRUX_CONFIG_STRING ||
        capacity > (size_t)CRUX_CONFIG_STRING_MAX + 1 || value->data.string.text == NULL ||
        value->data.string.length >= capacity ||
        overlaps(value->data.string.text, value->data.string.length, storage, capacity)) {
        return CRUX_CONFIG_ERR_INVALID_ARGUMENT;
    }
    if (!accepted(spec, capacity, value)) {
        return CRUX_CONFIG_ERR_REJECTED;
    }

    parameter->storage = storage;
    parameter->capacity = capacity;
    publish(parameter, value);
    parameter->spec = spec;
    return CRUX_CONFIG_OK;
}

const char *crux_config_key(const crux_config_param_t *parameter)
{
    return parameter != NULL && parameter->spec != NULL ? parameter->spec->key : NULL;
}

crux_config_result_t crux_config_read(const crux_config_param_t *parameter,
                                      crux_config_value_t *out)
{
    if (out == NULL) {
        return CRUX_CONFIG_ERR_INVALID_ARGUMENT;
    }
    crux_config_result_t result = ready(parameter);
    if (result == CRUX_CONFIG_OK) {
        *out = parameter->value;
    }
    return result;
}

crux_config_result_t crux_config_update(crux_config_param_t *parameter,
                                        const crux_config_value_t *candidate)
{
    if (candidate == NULL) {
        return CRUX_CONFIG_ERR_INVALID_ARGUMENT;
    }
    crux_config_result_t result = ready(parameter);
    if (result != CRUX_CONFIG_OK) {
        return result;
    }

    crux_config_value_t value = *candidate;
    if (!accepted(parameter->spec, parameter->capacity, &value)) {
        return CRUX_CONFIG_ERR_REJECTED;
    }

    publish(parameter, &value);
    return CRUX_CONFIG_OK;
}

/* Decode what the port returned. Every return path of the caller clears the wire buffer. */
static crux_config_result_t load_from_wire(crux_config_param_t *parameter, uint8_t *wire,
                                           size_t wire_size)
{
    crux_config_record_info_t record = {0};
    crux_config_result_t result =
        crux_config_record_read(parameter->spec->key, wire, wire_size, &record);
    size_t length = record.length, transferred = record.transferred;
    if (result == CRUX_CONFIG_DEFAULT_MISSING) {
        return fallback(parameter, CRUX_CONFIG_DEFAULT_MISSING);
    }
    if (result != CRUX_CONFIG_OK) {
        return port_error(result);
    }

    if (transferred > wire_size) {
        return CRUX_CONFIG_ERR_IO;
    }
    crux_config_type_t expected = parameter->spec->default_value.type;
    bool length_valid =
        expected == CRUX_CONFIG_STRING
            ? length >= HEADER_LENGTH && length - HEADER_LENGTH < parameter->capacity
            : length == BOOL_LENGTH || length == U32_LENGTH;
    if (!length_valid) {
        return fallback(parameter, CRUX_CONFIG_DEFAULT_REJECTED);
    }
    if (transferred != length) {
        return CRUX_CONFIG_ERR_IO;
    }

    uint16_t schema = (uint16_t)((uint16_t)wire[2] | (uint16_t)((uint16_t)wire[3] << 8));
    crux_config_type_t type = (crux_config_type_t)wire[1];
    if (wire[0] != 1 || schema != parameter->spec->schema_version || type != expected ||
        (type == CRUX_CONFIG_BOOL && (length != BOOL_LENGTH || wire[4] > 1)) ||
        (type == CRUX_CONFIG_U32 && length != U32_LENGTH)) {
        return fallback(parameter, CRUX_CONFIG_DEFAULT_REJECTED);
    }

    crux_config_value_t value = {.type = type};
    if (type == CRUX_CONFIG_BOOL) {
        value.data.boolean = wire[4] != 0;
    } else if (type == CRUX_CONFIG_U32) {
        value.data.u32 = (uint32_t)wire[4] | ((uint32_t)wire[5] << 8) | ((uint32_t)wire[6] << 16) |
                         ((uint32_t)wire[7] << 24);
    } else {
        /* The length check above leaves room: length is at most HEADER_LENGTH + STRING_MAX. */
        wire[length] = 0;
        value.data.string.text = (const char *)wire + HEADER_LENGTH;
        value.data.string.length = length - HEADER_LENGTH;
    }

    if (!accepted(parameter->spec, parameter->capacity, &value)) {
        return fallback(parameter, CRUX_CONFIG_DEFAULT_REJECTED);
    }

    publish(parameter, &value);
    return CRUX_CONFIG_OK;
}

crux_config_result_t crux_config_load(crux_config_param_t *parameter)
{
    crux_config_result_t result = ready(parameter);
    if (result != CRUX_CONFIG_OK) {
        return result;
    }

    uint8_t wire[WIRE_MAX] = {0};
    result = load_from_wire(parameter, wire, sizeof(wire));
    clear_wire(wire, sizeof(wire));
    return result;
}

crux_config_result_t crux_config_save(const crux_config_param_t *parameter)
{
    crux_config_result_t result = ready(parameter);
    if (result != CRUX_CONFIG_OK) {
        return result;
    }

    crux_config_value_t value = parameter->value;
    if (!accepted(parameter->spec, parameter->capacity, &value)) {
        return CRUX_CONFIG_ERR_REJECTED;
    }

    uint8_t wire[WIRE_MAX] = {1, (uint8_t)value.type, (uint8_t)parameter->spec->schema_version,
                              (uint8_t)(parameter->spec->schema_version >> 8)};
    size_t length = BOOL_LENGTH;
    if (value.type == CRUX_CONFIG_BOOL) {
        wire[4] = value.data.boolean ? 1 : 0;
    } else if (value.type == CRUX_CONFIG_U32) {
        length = U32_LENGTH;
        for (size_t i = 0; i < 4; ++i) {
            wire[4 + i] = (uint8_t)(value.data.u32 >> (8 * i));
        }
    } else {
        length = HEADER_LENGTH + value.data.string.length;
        memcpy(wire + HEADER_LENGTH, value.data.string.text, value.data.string.length);
    }

    result = crux_config_record_write(parameter->spec->key, wire, length);
    clear_wire(wire, sizeof(wire));
    return result == CRUX_CONFIG_OK ? result : port_error(result);
}
