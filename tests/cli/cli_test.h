/* Shared helpers for the CLI test suites. */
#ifndef CRUX_CLI_TEST_H
#define CRUX_CLI_TEST_H

#include "cli_internal.h"
#include "fake_events.h"
#include "fake_transport.h"

#include <stdio.h>
#include <string.h>

extern unsigned cli_test_checks, cli_test_failures;

#define CHECK(expression)                                                                          \
    do {                                                                                           \
        ++cli_test_checks;                                                                         \
        if (!(expression)) {                                                                       \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #expression);                           \
            ++cli_test_failures;                                                                   \
        }                                                                                          \
    } while (0)

/* Compares text, and shows both sides when they differ. */
#define CHECK_TEXT(actual, expected)                                                               \
    do {                                                                                           \
        ++cli_test_checks;                                                                         \
        const char *cli_actual = (actual);                                                         \
        const char *cli_expected = (expected);                                                     \
        if (strcmp(cli_actual, cli_expected) != 0) {                                               \
            printf("FAIL %s:%d:\n  got      [%s]\n  expected [%s]\n", __FILE__, __LINE__,          \
                   cli_actual, cli_expected);                                                      \
            ++cli_test_failures;                                                                   \
        }                                                                                          \
    } while (0)

/* Reports the totals, and is the suite's exit status. */
int cli_test_finish(const char *suite);

/* Available only in the instrumented core built for tests with CRUX_CLI_TESTING. */
void crux_cli_test_reset(void);

/* True when the bytes occur anywhere in the memory. */
bool cli_test_contains(const void *memory, size_t size, const char *needle);

#endif
