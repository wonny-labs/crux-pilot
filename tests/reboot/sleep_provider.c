/* The sleep provider of the reboot service, on Sync's semaphore, with the take wrapped so that its
 * answer is chosen. A sleep is a take that times out. A take that returns at once, with an error or
 * because the semaphore was given, has not slept, and the provider must not say that it did, or the
 * delay and the wait of the worker would collapse. */
#include <crux/status.h>
#include <crux/sync.h>

#include "reboot_port.h"

#include <stdio.h>

// NOLINTBEGIN(bugprone-reserved-identifier): GNU linker wrapping ABI
crux_status_t __real_crux_semaphore_take(crux_semaphore_t semaphore, uint32_t timeout_ms);
crux_status_t __wrap_crux_semaphore_take(crux_semaphore_t semaphore, uint32_t timeout_ms);
// NOLINTEND(bugprone-reserved-identifier)

static crux_status_t answer;
static uint32_t asked_for;
static unsigned takes;
static unsigned failures;

#define CHECK(expression)                                                                          \
    do {                                                                                           \
        if (!(expression)) {                                                                       \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #expression);                           \
            ++failures;                                                                            \
        }                                                                                          \
    } while (0)

crux_status_t __wrap_crux_semaphore_take(crux_semaphore_t semaphore, uint32_t timeout_ms)
{
    (void)semaphore;
    asked_for = timeout_ms;
    ++takes;
    return answer;
}

int main(void)
{
    /* A take that times out is a sleep, of the time that was asked for. */
    answer = CRUX_ERR_TIMEOUT;
    CHECK(crux_reboot_port_sleep(1000u));
    CHECK(asked_for == 1000u && takes == 1u);
    CHECK(crux_reboot_port_sleep(50u));
    CHECK(asked_for == 50u && takes == 2u);
    /* A take that fails at once has not slept. */
    answer = CRUX_ERR_IO;
    CHECK(!crux_reboot_port_sleep(50u));
    answer = CRUX_ERR_INVALID_ARGUMENT;
    CHECK(!crux_reboot_port_sleep(50u));
    answer = CRUX_ERR_INVALID_STATE;
    CHECK(!crux_reboot_port_sleep(50u));
    /* Nor has a take that got the semaphore, which nothing ever gives. */
    answer = CRUX_OK;
    CHECK(!crux_reboot_port_sleep(50u));
    printf("reboot_sleep_provider: %u failed\n", failures);
    return failures == 0u ? 0 : 1;
}
