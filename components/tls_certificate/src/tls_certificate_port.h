#ifndef CRUX_TLS_CERTIFICATE_PORT_H
#define CRUX_TLS_CERTIFICATE_PORT_H

#include <crux/tls_certificate.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/*
 * Private provider contract, implemented by ports/psa or a test composition. Every call comes from
 * thread context under the module's lock, so a provider may wait, and must not call back into this
 * module. Buffers never overlap.
 */

/* SHA-256 of data, which may be empty. Returns false if the provider failed. */
bool crux_tls_certificate_port_sha256(const uint8_t *data, size_t length, uint8_t digest[32]);

/*
 * ECDSA P-256 over a SHA-256 digest with the device's private scalar, as r then s, each 32 bytes
 * big-endian. The provider clears any copy of the scalar it made. Returns false if it failed.
 */
bool crux_tls_certificate_port_sign(const uint8_t private_key[32], const uint8_t digest[32],
                                    uint8_t signature[64]);

/* Random bytes, for serial numbers. Returns false if the source failed. */
bool crux_tls_certificate_port_random(uint8_t *buffer, size_t length);

/* Events, with the web.* identifiers they map to. */
typedef enum {
    CRUX_TLS_CERTIFICATE_EVENT_REPLACED, /* web.certificate_replaced, OK or FAILED. */
    CRUX_TLS_CERTIFICATE_EVENT_REJECTED  /* web.certificate_rejected, INVALID. */
} crux_tls_certificate_event_type_t;

/* Why, which becomes the record's subject. */
typedef enum {
    CRUX_TLS_CERTIFICATE_REASON_GENERATED,    /* "self_signed": first boot. */
    CRUX_TLS_CERTIFICATE_REASON_REGENERATED,  /* "self_signed": by an operator. */
    CRUX_TLS_CERTIFICATE_REASON_INSTALLED,    /* "installed". */
    CRUX_TLS_CERTIFICATE_REASON_SAVE_FAILED,  /* "save_failed", with REPLACED and FAILED. */
    CRUX_TLS_CERTIFICATE_REASON_MALFORMED,    /* "malformed". */
    CRUX_TLS_CERTIFICATE_REASON_INVALID,      /* "invalid". */
    CRUX_TLS_CERTIFICATE_REASON_KEY_MISMATCH, /* "key_mismatch". */
    CRUX_TLS_CERTIFICATE_REASON_TOO_LARGE,    /* "too_large". */
    CRUX_TLS_CERTIFICATE_REASON_STORED        /* "stored": the stored one, refused at start. */
} crux_tls_certificate_reason_t;

/* Holds no certificate or key bytes, and no client input but what Access validated. */
typedef struct {
    crux_tls_certificate_event_type_t type;
    crux_tls_certificate_reason_t reason;
    bool failed;           /* REPLACED only: the save failed. */
    uint8_t channel;       /* From crux_access_session_origin(), or 0 at start. */
    uint8_t source_length; /* 0, 4 or 8. */
    uint8_t source[16];
    char user[CRUX_ACCESS_NAME_SIZE]; /* Empty at start. */
} crux_tls_certificate_event_t;

/* Reports an event without waiting on storage. */
void crux_tls_certificate_port_event(const crux_tls_certificate_event_t *event);

#endif
