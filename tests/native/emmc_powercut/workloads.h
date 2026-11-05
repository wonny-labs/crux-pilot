/* SPDX-License-Identifier: Apache-2.0 */
/*
 * Workloads of the power-cut replay, deterministic from the step number. Each step that returns
 * success is followed by a trace mark whose id is the step number, so the number of marks before a
 * cut is the number of acknowledged steps. With early_ack the mark comes before the call that makes
 * the step durable, which is a deliberate defect for a control.
 */
#ifndef CRUX_TESTS_EMMC_POWERCUT_WORKLOADS_H
#define CRUX_TESTS_EMMC_POWERCUT_WORKLOADS_H

#include <stdbool.h>
#include <stdint.h>

enum powercut_workload { POWERCUT_W1, POWERCUT_W2, POWERCUT_W3, POWERCUT_W4 };

int powercut_prepare(enum powercut_workload w); /* baseline content, on the no-cache profile */
int powercut_run(enum powercut_workload w, bool early_ack);

struct powercut_result {
    bool mounted, data, read_error, replaced, untouched, recommit;
};

/* Expects the volume unmounted; mounts it without formatting and leaves it mounted. */
void powercut_verify(enum powercut_workload w, uint32_t acked, struct powercut_result *out);

#endif /* CRUX_TESTS_EMMC_POWERCUT_WORKLOADS_H */
