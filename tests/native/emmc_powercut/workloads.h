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

/* Baseline content, on the no-cache profile. W4 needs an image with the Settings File backend. */
int powercut_prepare(enum powercut_workload w);
int powercut_run(enum powercut_workload w, bool early_ack);

#define POWERCUT_NO_PATH UINT32_MAX
#define POWERCUT_STAGE_SKIP 2
/*
 * A test of the W3 check itself: makes the state after `steps` W3 steps, without marks, then leaves
 * the W3 file `empty_path` (an index in workloads.c, or POWERCUT_NO_PATH) with size 0. Returns
 * POWERCUT_STAGE_SKIP when that file's directory does not exist at that step.
 */
int powercut_stage_w3(uint32_t steps, uint32_t empty_path);

/*
 * A test of the W4 check itself, on an image after a complete run: "missing" removes the settings
 * file, "leftover" creates an empty temporary (.cmp) beside it, "none" changes nothing. Needs an
 * image with the Settings File backend, else -ENOTSUP.
 */
int powercut_stage_w4(const char *damage);

struct powercut_result {
    bool mounted, data, read_error, replaced, untouched, recommit;
    /* W4 only: the Settings File backend's temporary (.cmp) is left behind. Not a failure. */
    bool cmp_left;
};

/* Expects the volume unmounted; mounts it without formatting and leaves it mounted. */
void powercut_verify(enum powercut_workload w, uint32_t acked, struct powercut_result *out);

/*
 * By default, data allows a later unacknowledged step only as its complete result. With
 * acked_only, the W1 check asks only that every acknowledged record is intact and ignores what
 * follows, which is the design's I2. It changes nothing for W2, whose check already allows any
 * complete later generation, or for W3.
 */
void powercut_set_acked_only(bool acked_only);

/*
 * With create_may_be_empty, the W3 data check also accepts one state inside a step: the step after
 * the acknowledged one creates a file, which exists with size 0, and every other path is as after
 * the acknowledged step. A file system that commits on create, as Reliance Edge does in the Crux
 * profile, leaves this state at a cut between the create and the sync. Nothing else is accepted.
 * powercut_created_empty tells whether a verify accepted that state.
 */
void powercut_set_create_may_be_empty(bool create_may_be_empty);
bool powercut_created_empty(void);

#endif /* CRUX_TESTS_EMMC_POWERCUT_WORKLOADS_H */
