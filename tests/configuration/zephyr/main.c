#include <zephyr/ztest.h>
int crux_configuration_conformance(void);
int crux_configuration_string_conformance(void);
ZTEST(configuration_software, test_shared_fault_contract)
{
    zassert_equal(crux_configuration_conformance(), 0);
}
ZTEST(configuration_software, test_shared_string_contract)
{
    zassert_equal(crux_configuration_string_conformance(), 0);
}
ZTEST_SUITE(configuration_software, NULL, NULL, NULL, NULL, NULL);
