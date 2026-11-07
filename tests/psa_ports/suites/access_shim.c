/* Derive and keyed hash run the fake first, for its counters, hooks and faults, and then the real
 * provider, whose answer replaces the fake's. Random stays the fake's: see CMakeLists.txt. A lock
 * stands in for the core locks that TF-PSA-Crypto has no option for on Host, and a value a test
 * forces stays forced. */
#include "access_port.h"

#include <pthread.h>

static pthread_mutex_t provider_lock = PTHREAD_MUTEX_INITIALIZER;
static bool derive_is_forced, hmac_is_fixed;

bool psa_access_port_derive(uint32_t algorithm, uint32_t iterations, const uint8_t *salt,
                            const char *password, size_t length, uint8_t *derived);
bool psa_access_port_hmac(const uint8_t *key, const uint8_t *data, size_t length, uint8_t *hash);
bool __real_crux_access_port_derive(uint32_t algorithm, uint32_t iterations, const uint8_t *salt,
                                    const char *password, size_t length, uint8_t *derived);
bool __real_crux_access_port_hmac(const uint8_t *key, const uint8_t *data, size_t length,
                                  uint8_t *hash);
void __real_access_fake_derive_force(const uint8_t *derived);
void __real_access_fake_hmac_fixed(bool fixed);
bool __wrap_crux_access_port_derive(uint32_t algorithm, uint32_t iterations, const uint8_t *salt,
                                    const char *password, size_t length, uint8_t *derived);
bool __wrap_crux_access_port_hmac(const uint8_t *key, const uint8_t *data, size_t length,
                                  uint8_t *hash);
void __wrap_access_fake_derive_force(const uint8_t *derived);
void __wrap_access_fake_hmac_fixed(bool fixed);

void __wrap_access_fake_derive_force(const uint8_t *derived)
{
    derive_is_forced = derived != NULL;
    __real_access_fake_derive_force(derived);
}

void __wrap_access_fake_hmac_fixed(bool fixed)
{
    hmac_is_fixed = fixed;
    __real_access_fake_hmac_fixed(fixed);
}

bool __wrap_crux_access_port_derive(uint32_t algorithm, uint32_t iterations, const uint8_t *salt,
                                    const char *password, size_t length, uint8_t *derived)
{
    if (!__real_crux_access_port_derive(algorithm, iterations, salt, password, length, derived)) {
        return false;
    }
    if (derive_is_forced) {
        return true;
    }
    pthread_mutex_lock(&provider_lock);
    bool result = psa_access_port_derive(algorithm, iterations, salt, password, length, derived);
    pthread_mutex_unlock(&provider_lock);
    return result;
}

bool __wrap_crux_access_port_hmac(const uint8_t *key, const uint8_t *data, size_t length,
                                  uint8_t *hash)
{
    if (!__real_crux_access_port_hmac(key, data, length, hash)) {
        return false;
    }
    if (hmac_is_fixed) {
        return true;
    }
    pthread_mutex_lock(&provider_lock);
    bool result = psa_access_port_hmac(key, data, length, hash);
    pthread_mutex_unlock(&provider_lock);
    return result;
}
