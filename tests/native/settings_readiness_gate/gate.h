/* SPDX-License-Identifier: Apache-2.0 */
#ifndef READINESS_GATE_H
#define READINESS_GATE_H
#include <stdbool.h>
extern unsigned gate_reads, gate_writes, gate_erases, gate_denied, gate_fail_read;
extern int gate_write_error;
void gate_reset(void);
#endif
