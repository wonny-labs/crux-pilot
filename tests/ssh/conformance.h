/* The behaviour every SSH provider must show, written so that a provider can run it with its own
 * client. */
#ifndef CRUX_SSH_CONFORMANCE_H
#define CRUX_SSH_CONFORMANCE_H

#include "ssh_test_client.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    const char *name;
    void (*run)(const crux_ssh_test_client_t *client);
} crux_ssh_conformance_case_t;

extern const crux_ssh_conformance_case_t crux_ssh_conformance_cases[];
extern const size_t crux_ssh_conformance_case_count;

/** Runs before each case, to bring the provider and client back to a known state. */
typedef void (*crux_ssh_conformance_reset_t)(void);

/** Runs one case and returns the number of checks that failed, printing them when verbose. */
unsigned crux_ssh_conformance_run_case(const crux_ssh_conformance_case_t *test,
                                       const crux_ssh_test_client_t *client,
                                       crux_ssh_conformance_reset_t reset, bool verbose);

#ifdef __cplusplus
}
#endif

#endif /* CRUX_SSH_CONFORMANCE_H */
