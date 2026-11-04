/*
 * A product build of the store, which is the one library a composition gets, linked by mistake with
 * the test-only ports. Compiled without the test definitions, it refuses a test declaration at
 * start: no slot is read or written, and the state is UNUSABLE, so provisioning stays closed.
 * Either half of a test declaration is enough. A product core can start only once, so each
 * declaration runs in a process of its own.
 */
#include "credential_port.h"
#include "fake_file.h"
#include "fake_ports.h"

#include <stdio.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <unistd.h>

#define REQUIRE(expression)                                                                        \
    do {                                                                                           \
        if (!(expression)) {                                                                       \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #expression);                           \
            exit(1);                                                                               \
        }                                                                                          \
    } while (0)

void credential_medium_blank(void);

static const uint8_t data[] = "accounts";

static void start_with(const crux_credential_store_info_t *declaration)
{
    credential_fake_reset();
    credential_fake_declare(declaration);
    credential_medium_blank();
}

static void expect_refused(const crux_credential_store_info_t *declaration)
{
    uint8_t buffer[16];
    size_t length = 0;
    crux_credential_store_info_t info;

    start_with(declaration);
    REQUIRE(crux_credential_start(1, 2) == CRUX_CREDENTIAL_ERR_UNUSABLE);
    REQUIRE(crux_credential_state() == CRUX_CREDENTIAL_STATE_UNUSABLE);
    REQUIRE(crux_credential_store_get_info(&info) == CRUX_OK);
    REQUIRE(info.state == CRUX_CREDENTIAL_STATE_UNUSABLE);
    REQUIRE(info.basis == CRUX_CREDENTIAL_BASIS_UNDECLARED);
    REQUIRE(info.evidence == CRUX_CREDENTIAL_EVIDENCE_UNDECLARED);
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
}

/* The control: a declaration a real provider could make is accepted, and is reported as made. */
static void expect_accepted(const crux_credential_store_info_t *declaration)
{
    crux_credential_store_info_t info;

    start_with(declaration);
    REQUIRE(crux_credential_start(1, 2) == CRUX_CREDENTIAL_OK);
    REQUIRE(crux_credential_state() == CRUX_CREDENTIAL_STATE_EMPTY);
    REQUIRE(crux_credential_write(CRUX_CREDENTIAL_RECORD_ACCOUNTS, data, sizeof(data)) ==
            CRUX_CREDENTIAL_OK);
    REQUIRE(crux_credential_store_get_info(&info) == CRUX_OK);
    REQUIRE(info.state == CRUX_CREDENTIAL_STATE_VALID);
    REQUIRE(info.basis == declaration->basis);
    REQUIRE(info.evidence == declaration->evidence);
    REQUIRE(info.confidential_against_medium_reader ==
            declaration->confidential_against_medium_reader);
}

static void in_a_process(void (*expectation)(const crux_credential_store_info_t *),
                         const crux_credential_store_info_t *declaration)
{
    (void)fflush(stdout);
    pid_t child = fork();
    REQUIRE(child >= 0);
    if (child == 0) {
        expectation(declaration);
        (void)fflush(stdout);
        _exit(0);
    }
    int status = 0;
    REQUIRE(waitpid(child, &status, 0) == child);
    REQUIRE(WIFEXITED(status) && WEXITSTATUS(status) == 0);
}

int main(void)
{
    static const crux_credential_store_info_t refused[] = {
        {.basis = CRUX_CREDENTIAL_BASIS_NONE, .evidence = CRUX_CREDENTIAL_EVIDENCE_HOST_FAKE},
        {.basis = CRUX_CREDENTIAL_BASIS_NONE, .evidence = CRUX_CREDENTIAL_EVIDENCE_SOFTWARE},
        {.basis = CRUX_CREDENTIAL_BASIS_DEVICE_ID_KEY,
         .evidence = CRUX_CREDENTIAL_EVIDENCE_HOST_FAKE},
        {.basis = CRUX_CREDENTIAL_BASIS_HARDWARE_BOUND,
         .evidence = CRUX_CREDENTIAL_EVIDENCE_HOST_FAKE},
        {.basis = CRUX_CREDENTIAL_BASIS_DEVICE_ID_KEY,
         .evidence = CRUX_CREDENTIAL_EVIDENCE_SOFTWARE,
         .confidential_against_medium_reader = true},
    };
    static const crux_credential_store_info_t accepted[] = {
        {.basis = CRUX_CREDENTIAL_BASIS_DEVICE_ID_KEY,
         .evidence = CRUX_CREDENTIAL_EVIDENCE_SOFTWARE},
        {.basis = CRUX_CREDENTIAL_BASIS_HARDWARE_BOUND,
         .evidence = CRUX_CREDENTIAL_EVIDENCE_PHYSICAL,
         .confidential_against_medium_reader = true},
    };

    in_a_process(expect_refused, credential_fake_default_declaration());
    in_a_process(expect_refused, NULL);
    for (size_t index = 0; index < sizeof(refused) / sizeof(refused[0]); ++index) {
        in_a_process(expect_refused, &refused[index]);
    }
    for (size_t index = 0; index < sizeof(accepted) / sizeof(accepted[0]); ++index) {
        in_a_process(expect_accepted, &accepted[index]);
    }
    puts("A product build of the store refuses the test-only ports");
    return 0;
}
