/*
 * The device ID of the credential store's PSA provider, from hwinfo. It is an identifier and it is
 * not a secret. On native_sim it is 4 bytes, from the host's gethostid() or --device_id.
 */
#include "credential_psa.h"

#include <zephyr/drivers/hwinfo.h>

size_t crux_credential_psa_device_id(uint8_t id[CRUX_CREDENTIAL_PSA_DEVICE_ID_MAX])
{
    ssize_t length = hwinfo_get_device_id(id, CRUX_CREDENTIAL_PSA_DEVICE_ID_MAX);
    return length > 0 ? (size_t)length : 0u;
}
