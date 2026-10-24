/* SPDX-License-Identifier: Apache-2.0 */
#ifndef READINESS_GATE_H
#define READINESS_GATE_H
#include <stdbool.h>
extern unsigned gate_reads, gate_writes, gate_erases, gate_denied, gate_fail_read;
extern int gate_write_error;
void gate_reset(void);
/* Permanently closes native Configuration dispatch until explicit new-boot reset.
 * Reports whether a ready state was closed; a fault can never become ready. */
bool gate_end_maintenance(void);
/* Read-only shared readiness for the exclusive combined File/Config owner. */
bool gate_is_ready(void);
#endif
