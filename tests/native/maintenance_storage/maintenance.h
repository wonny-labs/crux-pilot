/* SPDX-License-Identifier: Apache-2.0 */
#ifndef MAINTENANCE_H
#define MAINTENANCE_H
#include <stdbool.h>
/* Private exclusive, caller-serialized simulator composition. Reset only at a
 * simulated new boot, with every portable/native File handle already retired. */
void maintenance_newboot(void);
bool maintenance_finish(void);
/* Explicit fixture provisioning permission; never inferred from media errors. */
void maintenance_provisioning(bool enabled);
extern unsigned maintenance_file_dispatches;
extern unsigned maintenance_media_writes, maintenance_media_erases, maintenance_media_denied;
#endif
