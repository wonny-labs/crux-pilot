/* SPDX-License-Identifier: Apache-2.0 */
#include "observation.h"
int main(void) {
    (void)crux_observation_run(CRUX_OBSERVATION_ID_STATUS);
    /* Result remains in internal RAM. No UART, shell, mount, provisioning or
     * hardware readout service. Any later probe/readback needs its own approval. */
    for (;;) {
        __asm__ volatile("nop");
    }
}
