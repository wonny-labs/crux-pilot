/*
 * PROTOTYPE, NOT ADOPTED, NOT FOR MERGE.
 * Exists so the team can review the proposed image-state contract in running code.
 */
#include "harness.h"

#include <stdio.h>

unsigned harness_checks;
unsigned harness_failures;
const char *harness_case = "(no case)";

void harness_check(bool ok, const char *expression, const char *file, int line)
{
    harness_checks++;
    if (!ok) {
        harness_failures++;
        printf("FAIL %s:%d [%s] %s\n", file, line, harness_case, expression);
    }
}

void harness_check_int(long expected, long actual, const char *expression, const char *file,
                       int line)
{
    harness_checks++;
    if (expected != actual) {
        harness_failures++;
        printf("FAIL %s:%d [%s] %s: expected %ld, got %ld\n", file, line, harness_case, expression,
               expected, actual);
    }
}

int main(void)
{
    printf("PROTOTYPE, not adopted, not for merge: image-state contract (M9, #297)\n");
    run_pure_tests();
    run_provider_tests();
    run_scenario_tests();
    run_codec_tests();
    printf("%u checks, %u failures\n", harness_checks, harness_failures);
    return harness_failures == 0 ? 0 : 1;
}
