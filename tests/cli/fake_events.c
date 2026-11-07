#include "fake_events.h"

#ifndef CRUX_CLI_TEST_COMPOSITION
#error "The fake event port is a test composition, and must not reach a product image"
#endif

#define CLI_FAKE_EVENTS_MAX 4096u

static crux_cli_event_t events[CLI_FAKE_EVENTS_MAX];
static size_t event_count;

void crux_cli_port_event(const crux_cli_event_t *event)
{
    if (event_count < CLI_FAKE_EVENTS_MAX) {
        events[event_count] = *event;
        event_count += 1;
    }
}

void cli_fake_events_clear(void)
{
    event_count = 0;
}

size_t cli_fake_event_count(void)
{
    return event_count;
}

size_t cli_fake_event_count_of(crux_cli_event_type_t type)
{
    size_t count = 0;
    for (size_t index = 0; index < event_count; ++index) {
        if (events[index].type == type) {
            count += 1;
        }
    }
    return count;
}

const crux_cli_event_t *cli_fake_event(size_t index)
{
    return index < event_count ? &events[index] : NULL;
}
