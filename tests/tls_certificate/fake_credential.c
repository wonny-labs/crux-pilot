/* The parts of the credential adapter that the module calls, as a table in RAM. TEST ONLY. */
#include "fakes.h"
#include "fixtures.h"

#include <string.h>

static bool key_present;
static crux_credential_key_type_t key_type;
static crux_credential_result_t ensure_result, write_result, key_read_result, cert_read_result;
static void (*write_hook)(void);
static unsigned ensure_count, write_count;
static uint8_t certificate[CRUX_CREDENTIAL_CERTIFICATE_BYTES_MAX];
static size_t certificate_length;

void fake_store_reset(void)
{
    key_present = false;
    key_type = CRUX_CREDENTIAL_KEY_ECDSA_P256;
    ensure_result = write_result = key_read_result = cert_read_result = CRUX_CREDENTIAL_OK;
    write_hook = NULL;
    ensure_count = write_count = 0;
    certificate_length = 0;
}

void fake_store_key_present(bool present, crux_credential_key_type_t type)
{
    key_present = present;
    key_type = type;
}

void fake_store_ensure_result(crux_credential_result_t result)
{
    ensure_result = result;
}

void fake_store_read_result(crux_credential_record_t record, crux_credential_result_t result)
{
    if (record == CRUX_CREDENTIAL_RECORD_TLS_KEY) {
        key_read_result = result;
    } else {
        cert_read_result = result;
    }
}

void fake_store_write_result(crux_credential_result_t result)
{
    write_result = result;
}

void fake_store_write_hook(void (*hook)(void))
{
    write_hook = hook;
}

unsigned fake_store_ensure_count(void)
{
    return ensure_count;
}

unsigned fake_store_write_count(void)
{
    return write_count;
}

const uint8_t *fake_store_certificate(size_t *length)
{
    *length = certificate_length;
    return certificate_length > 0u ? certificate : NULL;
}

void fake_store_set_certificate(const uint8_t *record, size_t length)
{
    memcpy(certificate, record, length);
    certificate_length = length;
}

crux_credential_result_t crux_credential_key_ensure(crux_credential_record_t record,
                                                    crux_credential_key_type_t type)
{
    ensure_count += 1u;
    if (record != CRUX_CREDENTIAL_RECORD_TLS_KEY) {
        return CRUX_CREDENTIAL_ERR_INVALID_ARGUMENT;
    }
    if (ensure_result != CRUX_CREDENTIAL_OK) {
        return ensure_result;
    }
    if (!key_present) {
        key_present = true;
        key_type = type;
    }
    return CRUX_CREDENTIAL_OK;
}

static size_t key_record(uint8_t *out)
{
    size_t public_length = key_type == CRUX_CREDENTIAL_KEY_ECDSA_P256 ? 65u : 32u;
    out[0] = (uint8_t)key_type;
    out[1] = (uint8_t)public_length;
    out[2] = 32;
    out[3] = 0;
    memcpy(out + 4, fixture_device_key, public_length);
    memcpy(out + 4 + public_length, fake_device_scalar, 32);
    return 4u + public_length + 32u;
}

crux_credential_result_t crux_credential_read(crux_credential_record_t record, void *buffer,
                                              size_t capacity, size_t *length)
{
    *length = 0;
    if (record == CRUX_CREDENTIAL_RECORD_TLS_KEY) {
        if (key_read_result != CRUX_CREDENTIAL_OK) {
            return key_read_result;
        }
        if (!key_present) {
            return CRUX_CREDENTIAL_ERR_NOT_FOUND;
        }
        uint8_t bytes[CRUX_CREDENTIAL_KEY_RECORD_BYTES_MAX];
        size_t size = key_record(bytes);
        *length = size;
        if (capacity < size) {
            return CRUX_CREDENTIAL_ERR_TOO_LARGE;
        }
        memcpy(buffer, bytes, size);
        memset(bytes, 0, sizeof(bytes));
        return CRUX_CREDENTIAL_OK;
    }
    if (record != CRUX_CREDENTIAL_RECORD_TLS_CERTIFICATE) {
        return CRUX_CREDENTIAL_ERR_INVALID_ARGUMENT;
    }
    if (cert_read_result != CRUX_CREDENTIAL_OK) {
        return cert_read_result;
    }
    if (certificate_length == 0u) {
        return CRUX_CREDENTIAL_ERR_NOT_FOUND;
    }
    *length = certificate_length;
    if (capacity < certificate_length) {
        return CRUX_CREDENTIAL_ERR_TOO_LARGE;
    }
    memcpy(buffer, certificate, certificate_length);
    return CRUX_CREDENTIAL_OK;
}

crux_credential_result_t crux_credential_write(crux_credential_record_t record, const void *data,
                                               size_t length)
{
    write_count += 1u;
    if (write_hook != NULL) {
        write_hook();
    }
    if (record != CRUX_CREDENTIAL_RECORD_TLS_CERTIFICATE || data == NULL || length == 0u) {
        return CRUX_CREDENTIAL_ERR_INVALID_ARGUMENT;
    }
    if (length > CRUX_CREDENTIAL_CERTIFICATE_BYTES_MAX) {
        return CRUX_CREDENTIAL_ERR_TOO_LARGE;
    }
    if (write_result != CRUX_CREDENTIAL_OK) {
        return write_result;
    }
    memcpy(certificate, data, length);
    certificate_length = length;
    return CRUX_CREDENTIAL_OK;
}

/* The store's own rule, restated: type, both lengths, a zero byte, then both parts. */
crux_credential_result_t crux_credential_key_parse(const void *record, size_t length,
                                                   crux_credential_key_view_t *view)
{
    const uint8_t *bytes = record;
    if (record == NULL || view == NULL || length < 4u) {
        return CRUX_CREDENTIAL_ERR_INVALID_ARGUMENT;
    }
    size_t public_length = bytes[0] == CRUX_CREDENTIAL_KEY_ECDSA_P256 ? 65u
                           : bytes[0] == CRUX_CREDENTIAL_KEY_ED25519  ? 32u
                                                                      : 0u;
    if (public_length == 0u || bytes[1] != public_length || bytes[2] != 32u || bytes[3] != 0u ||
        length != 4u + public_length + 32u) {
        return CRUX_CREDENTIAL_ERR_INVALID_ARGUMENT;
    }
    view->type = (crux_credential_key_type_t)bytes[0];
    view->public_key = bytes + 4;
    view->public_length = public_length;
    view->private_key = bytes + 4 + public_length;
    view->private_length = 32;
    return CRUX_CREDENTIAL_OK;
}
