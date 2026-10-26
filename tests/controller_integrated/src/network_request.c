/* Exercise the actual command publication boundary while simulating preemption. */
#include "network_client.h"
#include <string.h>
#include <zephyr/kernel.h>
#include <zephyr/shell/shell.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/ztest.h>

static unsigned int acknowledgements, rejections;
static void command_record(const char *format, ...);
static int unused_init(void)
{
    return 0;
}
static int unused_round(unsigned int trial)
{
    ARG_UNUSED(trial);
    return 0;
}
static void unused_report(void)
{
}

#define main controlled_network_probe_main
#define printk command_record
#define controller_network_init unused_init
#define controller_network_round unused_round
#define controller_network_report unused_report
#define CRUX_M3_SOURCE "controlled-request-fixture"
#include "../../../samples/controller/zephyr/network_probe.c"
#undef main

static void command_record(const char *format, ...)
{
    if (strstr(format, "M3_NET_REQUESTED") != NULL) {
        acknowledgements++;
        zassert_equal(atomic_get(&requested), -1);
        /* The real main loop's consume operation cannot run before the ACK ends. */
        zassert_false(atomic_cas(&requested, 1, 2));
        zassert_equal(request_round(NULL, 0, NULL), -EBUSY);
    } else if (strstr(format, "M3_NET_REJECTED") != NULL) {
        rejections++;
    }
}

ZTEST(network_request, test_ack_precedes_exactly_one_publication)
{
    atomic_set(&requested, 0);
    acknowledgements = rejections = 0;
    zassert_ok(request_round(NULL, 0, NULL));
    zassert_equal(acknowledgements, 1);
    zassert_equal(rejections, 1);
    zassert_true(atomic_cas(&requested, 1, 2));
    zassert_false(atomic_cas(&requested, 1, 2));
    zassert_equal(request_round(NULL, 0, NULL), -EBUSY);
    zassert_equal(acknowledgements, 1);
    zassert_equal(rejections, 2);
}
ZTEST_SUITE(network_request, NULL, NULL, NULL, NULL, NULL);
