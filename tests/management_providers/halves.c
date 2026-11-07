/* A composition with security events and no credential store, which the split of the no-provider
 * source allows. */
#include "mgmt_status_port.h"

#include <security_event_fake.h>

#include <stdio.h>

int main(void)
{
    const crux_security_event_stats_t stats = {.emitted = 7};
    crux_mgmt_status_event_counts_t counts = {0};
    crux_mgmt_status_credential_t words = {0};
    security_event_fake_stats_set(&stats);
    if (crux_mgmt_status_port_has_credential() ||
        crux_mgmt_status_port_credential(&words) != CRUX_ERR_NOT_SUPPORTED ||
        !crux_mgmt_status_port_has_security_events() ||
        crux_mgmt_status_port_security_events(&counts) != CRUX_OK || counts.emitted != 7u) {
        printf("FAIL: the halves do not answer as selected\n");
        return 1;
    }
    printf("management provider halves: passed\n");
    return 0;
}
