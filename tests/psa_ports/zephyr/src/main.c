#include "psa_port_cases.h"

#include <zephyr/ztest.h>

ZTEST(psa_ports, test_cases)
{
    zassert_equal(psa_port_cases_run(), 0u);
}

ZTEST_SUITE(psa_ports, NULL, NULL, NULL, NULL, NULL);
