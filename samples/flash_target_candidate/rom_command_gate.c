/* SPDX-License-Identifier: Apache-2.0 */
#include "rom_command.h"
#include "storage_adapter.h"
/* No elapsed source is selected. This leaf fails closed even if private lower
 * preparation is called outside the refused submission path. Tests replace
 * this leaf with deterministic elapsed time, never a target qualification flag. */
bool crux_rom_elapsed_expired(void) {
    return true;
}
int crux_rom_command_submit(const struct crux_rom_request *request) {
    (void)request;
    /* Identity/protection, elapsed deadline, watchdog and physical prerequisites
     * are unqualified. No copy, RAM entry or target command is admitted. */
    return -EACCES;
}

int crux_candidate_command_prerequisites(void) {
    /* Same outstanding physical/admission gates as submit. This never sets
     * device-ready, initializes a backend, or enters the command island. */
    return -EACCES;
}
