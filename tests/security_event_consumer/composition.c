/* A final link with the provider as objects, and ports a project supplies. These ports refuse all
 * work: the probe is about what links and about the first record, not about accounts. */
#include "access_port.h"

#include <crux/security_event.h>

#include <stdio.h>

bool crux_access_port_derive(uint32_t algorithm, uint32_t iterations,
                             const uint8_t salt[CRUX_ACCESS_SALT_SIZE], const char *password,
                             size_t length, uint8_t derived[CRUX_ACCESS_DERIVED_SIZE])
{
    (void)algorithm;
    (void)iterations;
    (void)salt;
    (void)password;
    (void)length;
    (void)derived;
    return false;
}

bool crux_access_port_random(uint8_t *output, size_t length)
{
    (void)output;
    (void)length;
    return false;
}

bool crux_access_port_hmac(const uint8_t key[CRUX_ACCESS_HASH_KEY_SIZE], const uint8_t *data,
                           size_t length, uint8_t hash[CRUX_ACCESS_HASH_SIZE])
{
    (void)key;
    (void)data;
    (void)length;
    (void)hash;
    return false;
}

uint64_t crux_access_port_now_ms(void)
{
    return 0;
}

crux_access_store_state_t crux_access_port_store_load(uint8_t *record, size_t capacity,
                                                      size_t *length)
{
    (void)record;
    (void)capacity;
    (void)length;
    return CRUX_ACCESS_STORE_UNUSABLE;
}

bool crux_access_port_store_save(const uint8_t *record, size_t length)
{
    (void)record;
    (void)length;
    return false;
}

/* The access core reports not-ready, and the record reaches the ring through the provider. */
int main(void)
{
    if (crux_security_event_init(NULL, 0) != CRUX_OK) {
        return 1;
    }
    const crux_access_session_t session = {1};
    if (crux_access_check(session, "status.read") != CRUX_ACCESS_ERR_NOT_READY) {
        return 2;
    }
    crux_security_event_stats_t stats;
    if (crux_security_event_get_stats(&stats) != CRUX_OK || stats.emitted != 1) {
        return 3;
    }
    /* The read path's dependency on access control, the other direction of the link. */
    uint32_t cursor = 0;
    crux_security_event_record_t record;
    if (crux_security_event_read(session, &cursor, &record) != CRUX_SECURITY_READ_ERR_NOT_READY) {
        return 4;
    }
    puts("composition linked and ran");
    return 0;
}
