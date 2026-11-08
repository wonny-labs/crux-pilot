/* Private to the network routes and their Host tests. */
#ifndef CRUX_WEB_MANAGEMENT_NETWORK_H
#define CRUX_WEB_MANAGEMENT_NETWORK_H

#include <crux/network_settings.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* The request body of PUT /api/v1/network is at most this long. Over it, the web core answers 413
 * before any session lookup. The longest valid body is under 100 bytes, so this leaves room for
 * white space and escapes, and no more. */
#define WEB_MGMT_NETWORK_BODY_MAX 256u

/*
 * Reads the body of PUT /api/v1/network: one object with the members `address` and `prefix_length`
 * and optionally `gateway`, and nothing else.
 *
 * - `address` and `gateway` are strings that hold a dotted quad, escapes included.
 * - `prefix_length` is an integer with no fraction and no exponent. A negative one reads as 0, and
 *   one too large for 32 bits as the largest, so that the rules of the service refuse them with a
 *   field error and not as a malformed body.
 * - No repeated or other member, no null, boolean, array or object, and no text after the object.
 *
 * The reader allocates nothing and does not recurse. Whether the settings are usable is the
 * service's to say, not this reader's.
 *
 * @param[out] settings Unchanged on failure.
 * @return False for a body that is not of that form.
 */
bool web_mgmt_parse_network(const uint8_t *body, size_t length, crux_network_settings_t *settings);

#endif
