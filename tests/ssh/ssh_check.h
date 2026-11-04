/* The smallest check for the SSH test programs that have no conformance case behind them. */
#ifndef CRUX_SSH_CHECK_H
#define CRUX_SSH_CHECK_H

#include <stdio.h>

static unsigned ssh_check_failures;

#define SSH_CHECK(expression)                                                                      \
    do {                                                                                           \
        if (!(expression)) {                                                                       \
            printf("FAIL line %d: %s\n", __LINE__, #expression);                                   \
            ++ssh_check_failures;                                                                  \
        }                                                                                          \
    } while (0)

#endif /* CRUX_SSH_CHECK_H */
