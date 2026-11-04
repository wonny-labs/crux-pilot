/* Runs every conformance case against the fake provider. */
#include "conformance.h"
#include "fake_provider.h"

#include <stdio.h>

int main(void)
{
    unsigned failed_cases = 0;

    for (size_t i = 0; i < crux_ssh_conformance_case_count; ++i) {
        const crux_ssh_conformance_case_t *test = &crux_ssh_conformance_cases[i];
        unsigned failures =
            crux_ssh_conformance_run_case(test, crux_ssh_fake_client(), crux_ssh_fake_reset, true);

        printf("%-48s %s\n", test->name, failures == 0 ? "ok" : "FAILED");
        failed_cases += failures != 0;
    }
    printf("%zu cases, %u failed\n", crux_ssh_conformance_case_count, failed_cases);
    return failed_cases == 0 ? 0 : 1;
}
