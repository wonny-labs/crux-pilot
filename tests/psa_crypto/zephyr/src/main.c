/*
 * Zephyr native_sim harness for the provider-neutral PSA Crypto conformance cases.
 * The provider comes from the modules given to the build, never from here.
 */
#include <zephyr/ztest.h>

#include "psa_cases.h"

#define PSA_ZTEST(name)                                                                            \
    ZTEST(psa_conformance, test_##name)                                                            \
    {                                                                                              \
        const char *failure = psa_case_##name();                                                   \
        const char *note = psa_case_note();                                                        \
                                                                                                   \
        if (note != NULL) {                                                                        \
            TC_PRINT("note: %s\n", note);                                                          \
        }                                                                                          \
        zassert_is_null(failure, "%s", failure);                                                   \
    }

PSA_CASE_LIST(PSA_ZTEST)

ZTEST(psa_conformance, test_case_list_is_complete)
{
    zassert_equal(PSA_CASE_COUNT, PSA_CASE_EXPECTED_COUNT, "case list changed");
}

ZTEST_SUITE(psa_conformance, NULL, NULL, NULL, NULL, NULL);
