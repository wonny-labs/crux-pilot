/*
 * The random and hash ports of the web session module over PSA Crypto. Only this file sees the
 * provider's header. Session tokens, CSRF tokens and attempt identifiers all come from
 * psa_generate_random(), so their strength is the provider's random generator.
 */
#include "web_session_port.h"

#include <psa/crypto.h>

/*
 * A test generator repeats, so a board must never build with one. native_sim has no other, and is
 * declared test-only: a token's strength is not shown there. See docs/psa-crypto-design.md.
 */
#if !defined(CONFIG_BOARD_NATIVE_SIM) &&                                                           \
    (defined(CONFIG_FAKE_ENTROPY_NATIVE_SIM) || defined(CONFIG_TEST_RANDOM_GENERATOR))
#error "A test random generator must not reach a build of these ports for a board"
#endif

bool crux_web_session_port_random(uint8_t *output, size_t length)
{
    return psa_crypto_init() == PSA_SUCCESS && psa_generate_random(output, length) == PSA_SUCCESS;
}

bool crux_web_session_port_hash(const uint8_t *data, size_t length,
                                uint8_t digest[CRUX_WEB_SESSION_HASH_SIZE])
{
    size_t written = 0;

    return psa_crypto_init() == PSA_SUCCESS &&
           psa_hash_compute(PSA_ALG_SHA_256, data, length, digest, CRUX_WEB_SESSION_HASH_SIZE,
                            &written) == PSA_SUCCESS &&
           written == CRUX_WEB_SESSION_HASH_SIZE;
}
