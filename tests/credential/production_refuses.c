/*
 * A product build of the store, which is the one library a composition gets, linked by mistake with
 * the test-only ports. Compiled without the test definitions, it refuses the fake's declaration at
 * start: no slot is read or written, and the state is UNUSABLE, so provisioning stays closed.
 */
#include "credential_port.h"
#include "fake_file.h"
#include "fake_ports.h"

#include <stdio.h>
#include <stdlib.h>

#define REQUIRE(expression)                                                                        \
    do {                                                                                           \
        if (!(expression)) {                                                                       \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #expression);                           \
            exit(1);                                                                               \
        }                                                                                          \
    } while (0)

void credential_medium_blank(void);

int main(void)
{
    static const uint8_t data[] = "accounts";
    uint8_t buffer[16];
    size_t length = 0;
    crux_credential_store_info_t info;

    credential_fake_reset();
    credential_medium_blank();
    REQUIRE(crux_credential_start(1, 2) == CRUX_CREDENTIAL_ERR_UNUSABLE);
    REQUIRE(crux_credential_state() == CRUX_CREDENTIAL_STATE_UNUSABLE);
    REQUIRE(crux_credential_store_get_info(&info) == CRUX_OK);
    REQUIRE(info.state == CRUX_CREDENTIAL_STATE_UNUSABLE);
    REQUIRE(info.basis == CRUX_CREDENTIAL_BASIS_NONE);
    REQUIRE(!info.confidential_against_medium_reader);
    REQUIRE(crux_credential_read(CRUX_CREDENTIAL_RECORD_ACCOUNTS, buffer, sizeof(buffer),
                                 &length) == CRUX_CREDENTIAL_ERR_UNUSABLE);
    REQUIRE(crux_credential_write(CRUX_CREDENTIAL_RECORD_ACCOUNTS, data, sizeof(data)) ==
            CRUX_CREDENTIAL_ERR_UNUSABLE);
    REQUIRE(crux_credential_key_ensure(CRUX_CREDENTIAL_RECORD_TLS_KEY,
                                       CRUX_CREDENTIAL_KEY_ECDSA_P256) ==
            CRUX_CREDENTIAL_ERR_UNUSABLE);
    REQUIRE(credential_fake_keygen_count() == 0u);
    REQUIRE(credential_fake_nonce_count() == 0u);
    REQUIRE(crux_file_fake_calls(CRUX_FILE_FAKE_STAT) == 0u);
    REQUIRE(crux_file_fake_calls(CRUX_FILE_FAKE_OPEN) == 0u);
    REQUIRE(!crux_file_fake_exists(1, "store.img") && !crux_file_fake_exists(2, "store.img"));
    puts("A product build of the store refuses the test-only ports");
    return 0;
}
