/*
 * The device ID of the credential store's PSA provider, from hwinfo. It is an identifier and it is
 * not a secret. On native_sim it is 4 bytes, from the host's gethostid() or --device_id.
 */
#include "credential_psa.h"

#include <zephyr/drivers/hwinfo.h>

/*
 * Keys from a test generator repeat, so a board must never build with one. native_sim has no other,
 * and is declared test-only: see docs/psa-crypto-design.md.
 */
#if !defined(CONFIG_BOARD_NATIVE_SIM) &&                                                           \
    (defined(CONFIG_FAKE_ENTROPY_NATIVE_SIM) || defined(CONFIG_TEST_RANDOM_GENERATOR))
#error "A test random generator must not reach a build of the credential store for a board"
#endif

size_t crux_credential_psa_device_id(uint8_t id[CRUX_CREDENTIAL_PSA_DEVICE_ID_MAX])
{
    ssize_t length = hwinfo_get_device_id(id, CRUX_CREDENTIAL_PSA_DEVICE_ID_MAX);
    return length > 0 ? (size_t)length : 0u;
}
