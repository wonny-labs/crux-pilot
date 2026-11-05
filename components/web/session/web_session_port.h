/* What the session module needs from its platform, beside the web clock and crux::sync and
 * crux::thread. components/web/ports/psa implements it on PSA Crypto. */
#ifndef CRUX_WEB_SESSION_PORT_H
#define CRUX_WEB_SESSION_PORT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define CRUX_WEB_SESSION_HASH_SIZE 32u

/* The one source of session tokens, CSRF tokens and attempt identifiers: a cryptographic random
 * generator. False on any failure, and the output is then not used. Any thread. */
bool crux_web_session_port_random(uint8_t *output, size_t length);

/* SHA-256 of a token, so that the table holds no token. Any thread, and quick. */
bool crux_web_session_port_hash(const uint8_t *data, size_t length,
                                uint8_t digest[CRUX_WEB_SESSION_HASH_SIZE]);

#endif /* CRUX_WEB_SESSION_PORT_H */
