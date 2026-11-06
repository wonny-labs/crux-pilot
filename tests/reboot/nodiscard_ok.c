/* The start of the service, with its result used: this compiles under the warnings of the build. */
#include <crux/reboot.h>

bool reboot_nodiscard_ok(const crux_reboot_config_t *config);

bool reboot_nodiscard_ok(const crux_reboot_config_t *config)
{
    return crux_reboot_start(config) == CRUX_REBOOT_OK;
}
