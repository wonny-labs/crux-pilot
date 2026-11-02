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

    printf("provider: %s\n", CRUX_PSA_PROVIDER_NAME);
    for (size_t i = 0; i < count; i++) {
        const char *failure = entries[i].run();

        if (failure == NULL) {
            printf("PASS %s\n", entries[i].name);
        } else {
            printf("FAIL %s: %s\n", entries[i].name, failure);
            failed++;
        }
    }
    printf("%zu cases, %zu failed\n", count, failed);
    if (count != PSA_CASE_EXPECTED_COUNT) {
        printf("case list has %zu entries, expected %d\n", count, PSA_CASE_EXPECTED_COUNT);
        return 2;
    }
    return failed == 0 ? 0 : 1;
}
