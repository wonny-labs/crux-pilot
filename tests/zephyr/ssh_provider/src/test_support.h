/* What the parts of this test image share. Test only. */
#ifndef CRUX_SSH_PROVIDER_TEST_SUPPORT_H
#define CRUX_SSH_PROVIDER_TEST_SUPPORT_H

#include "crux_ssh_provider.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* test_access_ports.c */
void test_access_derive_delay_ms(int32_t delay_ms);
unsigned test_access_derive_count(void);

/* test_keys.c: keys from wolfCrypt on native_sim's test-only entropy, never stored. */
typedef struct {
    crux_ssh_key_type_t type;
    uint8_t host_key[97]; /* The provider's form: private part, then public part. */
    size_t host_key_length;
    uint8_t blob[128]; /* The SSH public key blob. */
    size_t blob_length;
} test_key_t;

bool test_key_generate(crux_ssh_key_type_t type, test_key_t *key);
/* Signs data as a user key signs a login request, with the SSH signature blob as the result. */
bool test_key_sign(const test_key_t *key, const char *algorithm, const uint8_t *data, size_t length,
                   bool good_signature, uint8_t *signature, size_t capacity, size_t *written);
void test_key_print_fingerprint(const test_key_t *key);

/* test_sessions.c: stand-ins for the CLI of #193 and the SFTP adapter of #194. */
void test_sessions_fill(crux_ssh_callbacks_t *callbacks);
void test_sessions_wait_for_stop(void);
void test_print_measurements(void);

/* conformance_client.c */
int test_conformance_run(const char *only);

#endif /* CRUX_SSH_PROVIDER_TEST_SUPPORT_H */
