#include "controller_system.h"
#include "runtime.h"
#include <zephyr/kernel.h>
/* Cross-build composition only. Boot retains observations; never arm/feed/clear/
 * reset automatically. Any future physical commands need separate approval. */
static controller_system_t boot_diagnostics;
static system_runtime_observation_t c_observation, cpp_observation;
int main(void)
{
    if (controller_system_boot(&boot_diagnostics) != CRUX_OK ||
        system_runtime_observe(&c_observation) != CRUX_OK ||
        system_cpp_observe(&cpp_observation) != CRUX_OK)
        return 1;
    if (IS_ENABLED(CONFIG_BOARD_NATIVE_SIM)) {
        if (boot_diagnostics.initial_cause_status != CRUX_OK ||
            c_observation.cause_status != CRUX_OK || cpp_observation.cause_status != CRUX_OK ||
            boot_diagnostics.initial_cause.causes != CRUX_RESET_CAUSE_POR ||
            c_observation.cause.causes != CRUX_RESET_CAUSE_POR ||
            cpp_observation.cause.causes != CRUX_RESET_CAUSE_POR)
            return 2;
        printk("System C17/C++17 native observations retained; no lifecycle effects\n");
    }
    return 0;
}
