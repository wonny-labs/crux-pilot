/*
 * What the PSA provider of the credential store takes from its environment. A composition supplies
 * it: the Zephyr one reads hwinfo, in ports/zephyr. The result is an identifier and it is not a
 * secret, so this is not secure storage: see docs/credential-store.md#limits.
 */
#ifndef CRUX_CREDENTIAL_PSA_H
#define CRUX_CREDENTIAL_PSA_H

#include <stddef.h>
#include <stdint.h>

#define CRUX_CREDENTIAL_PSA_DEVICE_ID_MAX 32u

/* Returns the number of bytes written, or 0 if the device has no ID, and never a fixed constant. */
size_t crux_credential_psa_device_id(uint8_t id[CRUX_CREDENTIAL_PSA_DEVICE_ID_MAX]);

#endif
