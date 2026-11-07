/* Private to the service and its Host tests: the pure decision of the boot. */
#ifndef CRUX_NETWORK_SETTINGS_INTERNAL_H
#define CRUX_NETWORK_SETTINGS_INTERNAL_H

#include <crux/configuration.h>
#include <crux/network_settings.h>

/* What the five loads returned, and what the parameters then held. */
typedef struct {
    crux_config_result_t address_load;
    crux_config_result_t prefix_load;
    crux_config_result_t gateway_load;
    crux_config_result_t check_load;
    crux_config_result_t flag_load;
    uint32_t address;
    uint32_t prefix_length;
    uint32_t gateway;
    uint32_t check;
    bool use_saved;
} network_stored_t;

/*
 * Decides whether the stored set may apply. Returns CRUX_NETWORK_BOOT_SAVED and fills `values`
 * only when it may, and otherwise the reason it may not.
 */
crux_network_boot_reason_t network_decide(const network_stored_t *stored,
                                          crux_network_settings_t *values);

#ifdef CRUX_NETWORK_SETTINGS_TESTING
/*
 * What a reboot would decide on the store as it now is. It loads five scratch parameters with the
 * code that boot runs, applies nothing and leaves the service as it is.
 */
crux_network_boot_reason_t network_test_reboot(crux_network_settings_t *values);
#endif

#endif
