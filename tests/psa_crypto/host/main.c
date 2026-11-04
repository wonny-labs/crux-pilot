/*
 * Linux Host harness for the provider-neutral PSA Crypto conformance cases.
 * The provider is chosen by the CMake composition, never here.
 */
#include <stddef.h>
#include <stdio.h>

#include "../cases/psa_cases.h"

#ifndef CRUX_PSA_PROVIDER_NAME
#define CRUX_PSA_PROVIDER_NAME "unnamed"
#endif

struct entry {
    const char *name;
    const char *(*run)(void);
};

static const struct entry entries[] = {
#define PSA_ENTRY(name) {#name, psa_case_##name},
    PSA_CASE_LIST(PSA_ENTRY)
#undef PSA_ENTRY
};

int main(void)
{
    const size_t count = sizeof entries / sizeof entries[0];
    size_t failed = 0;
    size_t unsupported = 0;
    size_t deviations = 0;

    printf("provider: %s\n", CRUX_PSA_PROVIDER_NAME);
    psa_case_print_random_probe();
    for (size_t i = 0; i < count; i++) {
        const char *result = entries[i].run();
        const char *text = NULL;

        switch (psa_case_outcome(entries[i].name, result, &text)) {
        case PSA_OUTCOME_PASS: {
            const char *note = psa_case_note();

            if (note != NULL) {
                printf("PASS %s (note: %s)\n", entries[i].name, note);
            } else {
                printf("PASS %s\n", entries[i].name);
            }
            break;
        }
        case PSA_OUTCOME_DECLARED_UNSUPPORTED:
            printf("UNSUPPORTED %s (declared)\n", entries[i].name);
            unsupported++;
            break;
        case PSA_OUTCOME_DECLARED_DEVIATION:
            printf("DEVIATION %s (declared): %s\n", entries[i].name, text);
            deviations++;
            break;
        case PSA_OUTCOME_FAIL:
        default:
            printf("FAIL %s: %s\n", entries[i].name, text);
            failed++;
            break;
        }
    }
    printf("%zu cases, %zu failed, %zu declared unsupported, %zu declared deviations\n", count,
           failed, unsupported, deviations);
    if (count != PSA_CASE_EXPECTED_COUNT) {
        printf("case list has %zu entries, expected %d\n", count, PSA_CASE_EXPECTED_COUNT);
        return 2;
    }
    return failed == 0 ? 0 : 1;
}
