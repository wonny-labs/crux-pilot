/*
 * Zephyr native_sim harness for the provider-neutral PSA Crypto conformance cases.
 * The provider comes from the modules given to the build, never from here.
 */
#include <zephyr/ztest.h>

#include "psa_cases.h"

#define PSA_ZTEST(name)                                                                            \
    ZTEST(psa_conformance, test_##name)                                                            \
    {                                                                                              \
        const char *text = NULL;                                                                   \
        const psa_outcome_t outcome = psa_case_outcome(#name, psa_case_##name(), &text);           \
        const char *note = psa_case_note();                                                        \
                                                                                                   \
        if (note != NULL) {                                                                        \
            TC_PRINT("note: %s\n", note);                                                          \
        }                                                                                          \
        if (outcome == PSA_OUTCOME_DECLARED_UNSUPPORTED) {                                         \
            TC_PRINT("UNSUPPORTED %s (declared)\n", #name);                                        \
            ztest_test_skip();                                                                     \
        }                                                                                          \
        if (outcome == PSA_OUTCOME_DECLARED_DEVIATION) {                                           \
            TC_PRINT("DEVIATION %s (declared): %s\n", #name, text);                                \
            ztest_test_skip();                                                                     \
        }                                                                                          \
        zassert_equal(outcome, PSA_OUTCOME_PASS, "%s", text);                                      \
    }

PSA_CASE_LIST(PSA_ZTEST)

ZTEST(psa_conformance, test_case_list_is_complete)
{
    zassert_equal(PSA_CASE_COUNT, PSA_CASE_EXPECTED_COUNT, "case list changed");
}

static void *suite_setup(void)
{
    psa_case_print_random_probe();
    return NULL;
}

ZTEST_SUITE(psa_conformance, NULL, suite_setup, NULL, NULL, NULL);
