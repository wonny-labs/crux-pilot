#include "configuration_port.h"
#include <crux/configuration.h>

enum { KEY_LIMIT = 31, WIRE_MAX = 8, BOOL_LENGTH = 5, U32_LENGTH = 8 };

static bool valid_type(crux_config_type_t type)
{
    return type == CRUX_CONFIG_BOOL || type == CRUX_CONFIG_U32;
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
static bool accepted(const crux_config_spec_t *spec, const crux_config_value_t *value)
{
    return valid_type(value->type) && value->type == spec->default_value.type &&
           (spec->validate == NULL || spec->validate(value, spec->validation_context));
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
    if (!accepted(parameter->spec, &value)) {
        return CRUX_CONFIG_ERR_REJECTED;
    }
    parameter->value = value;
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
    if (!valid_key(spec->key) || !valid_type(spec->default_value.type)) {
        return CRUX_CONFIG_ERR_INVALID_ARGUMENT;
    }
    crux_config_value_t value = spec->default_value;
    if (!accepted(spec, &value)) {
        return CRUX_CONFIG_ERR_REJECTED;
    }
    parameter->value = value;
    parameter->spec = spec;
    return CRUX_CONFIG_OK;
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
    if (!accepted(parameter->spec, &value)) {
        return CRUX_CONFIG_ERR_REJECTED;
    }
    parameter->value = value;
    return CRUX_CONFIG_OK;
}
crux_config_result_t crux_config_load(crux_config_param_t *parameter)
{
    crux_config_result_t result = ready(parameter);
    if (result != CRUX_CONFIG_OK) {
        return result;
    }
    uint8_t wire[WIRE_MAX] = {0};
    crux_config_record_info_t record = {0};
    result = crux_config_port_read(parameter->spec->key, wire, sizeof(wire), &record);
    size_t length = record.length, transferred = record.transferred;
    if (result == CRUX_CONFIG_DEFAULT_MISSING) {
        return fallback(parameter, CRUX_CONFIG_DEFAULT_MISSING);
    }
    if (result != CRUX_CONFIG_OK) {
        return port_error(result);
    }
    if (transferred > sizeof(wire)) {
        return CRUX_CONFIG_ERR_IO;
    }
    if (length != BOOL_LENGTH && length != U32_LENGTH) {
        return fallback(parameter, CRUX_CONFIG_DEFAULT_REJECTED);
    }
    if (transferred != length) {
        return CRUX_CONFIG_ERR_IO;
    }
    uint16_t schema = (uint16_t)((uint16_t)wire[2] | (uint16_t)((uint16_t)wire[3] << 8));
    crux_config_type_t type = (crux_config_type_t)wire[1];
    if (wire[0] != 1 || schema != parameter->spec->schema_version || !valid_type(type) ||
        type != parameter->spec->default_value.type ||
        (type == CRUX_CONFIG_BOOL && (length != BOOL_LENGTH || wire[4] > 1)) ||
        (type == CRUX_CONFIG_U32 && length != U32_LENGTH)) {
        return fallback(parameter, CRUX_CONFIG_DEFAULT_REJECTED);
    }
    crux_config_value_t value = {.type = type};
    if (type == CRUX_CONFIG_BOOL) {
        value.data.boolean = wire[4] != 0;
    } else {
        value.data.u32 = (uint32_t)wire[4] | ((uint32_t)wire[5] << 8) | ((uint32_t)wire[6] << 16) |
                         ((uint32_t)wire[7] << 24);
    }
    if (!accepted(parameter->spec, &value)) {
        return fallback(parameter, CRUX_CONFIG_DEFAULT_REJECTED);
    }
    parameter->value = value;
    return CRUX_CONFIG_OK;
}
crux_config_result_t crux_config_save(const crux_config_param_t *parameter)
{
    crux_config_result_t result = ready(parameter);
    if (result != CRUX_CONFIG_OK) {
        return result;
    }
    crux_config_value_t value = parameter->value;
    if (!accepted(parameter->spec, &value)) {
        return CRUX_CONFIG_ERR_REJECTED;
    }
    uint8_t wire[WIRE_MAX] = {1, (uint8_t)value.type, (uint8_t)parameter->spec->schema_version,
                              (uint8_t)(parameter->spec->schema_version >> 8)};
    size_t length = BOOL_LENGTH;
    if (value.type == CRUX_CONFIG_BOOL) {
        wire[4] = value.data.boolean ? 1 : 0;
    } else {
        length = U32_LENGTH;
        for (size_t i = 0; i < 4; ++i) {
            wire[4 + i] = (uint8_t)(value.data.u32 >> (8 * i));
        }
    }
    result = crux_config_port_write(parameter->spec->key, wire, length);
    return result == CRUX_CONFIG_OK ? result : port_error(result);
}
