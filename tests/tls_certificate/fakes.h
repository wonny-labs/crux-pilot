/*
 * Test doubles for the TLS certificate core. TEST ONLY: the signature is not ECDSA, the random
 * source counts, and the credential adapter is a table in RAM with fault injection.
 */
#ifndef TLS_CERTIFICATE_FAKES_H
#define TLS_CERTIFICATE_FAKES_H

#include "tls_certificate_port.h"

#include <crux_credential_adapter.h>

/* ---- Ports. */

void fake_ports_reset(void);
void fake_ports_hash_fail(bool fail);
void fake_ports_sign_fail(bool fail);
void fake_ports_random_fail(bool fail);
/* The scalar the last signature used, and how many signatures were made. */
const uint8_t *fake_ports_last_sign_key(void);
const uint8_t *fake_ports_last_sign_digest(void);
unsigned fake_ports_sign_count(void);

size_t fake_events_count(void);
const crux_tls_certificate_event_t *fake_event(size_t index);
const crux_tls_certificate_event_t *fake_event_last(void);
/* Runs inside each event until cleared with NULL. */
void fake_events_hook(void (*hook)(const crux_tls_certificate_event_t *event));

/* ---- Credential adapter. */

/* The scalar of the fake device key, whose public point is fixture_device_key. */
extern const uint8_t fake_device_scalar[32];

void fake_store_reset(void);
/* What key_ensure finds: absent until it generates, or present already. */
void fake_store_key_present(bool present, crux_credential_key_type_t type);
void fake_store_ensure_result(crux_credential_result_t result);
void fake_store_read_result(crux_credential_record_t record, crux_credential_result_t result);
void fake_store_write_result(crux_credential_result_t result);
/* Runs inside each write, before it takes effect, until cleared with NULL. */
void fake_store_write_hook(void (*hook)(void));
unsigned fake_store_ensure_count(void);
unsigned fake_store_write_count(void);
/* The stored certificate record, or NULL with length zero when it is absent. */
const uint8_t *fake_store_certificate(size_t *length);
void fake_store_set_certificate(const uint8_t *record, size_t length);

#endif
