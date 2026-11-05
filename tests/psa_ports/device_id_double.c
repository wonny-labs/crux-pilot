/* A device ID that a test controls. The real one, from hwinfo, runs in the management composition. */
#include "credential_psa.h"
#include "psa_port_cases.h"

#include <string.h>

static uint8_t device_id[CRUX_CREDENTIAL_PSA_DEVICE_ID_MAX];
static size_t device_id_length;

void psa_port_test_device_id(const uint8_t *id, size_t length)
{
    device_id_length = length;
    if (length > 0u) {
        memcpy(device_id, id, length);
    }
}

size_t crux_credential_psa_device_id(uint8_t id[CRUX_CREDENTIAL_PSA_DEVICE_ID_MAX])
{
    memcpy(id, device_id, device_id_length);
    return device_id_length;
}
