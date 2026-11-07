/* SPDX-License-Identifier: Apache-2.0 */
/*
 * One program, phases selected on the command line, run as separate processes by run_replay.py on
 * the eMMC simulator's host image.
 *   provision  format and write the baseline content of every workload
 *   run        run a workload, with a trace mark after each acknowledged step
 *   run-early  the same, with each mark before the step's sync: a deliberate defect for a control
 *   verify     mount without formatting, check the invariants and print one POWERCUT line;
 *              --acked-only narrows the W1 data check to the acknowledged records
 */
#include "fs_backend.h"
#include "workloads.h"

#include <cmdline.h>
#include <posix_board_if.h>
#include <posix_native_task.h>

#include <stdio.h>
#include <string.h>
#include <zephyr/sys/util.h>

static const char *phase;
static const char *workload;
static uint32_t acked;
static bool acked_only;

static void add_options(void)
{
    static struct args_struct_t options[] = {
        {.option = "phase",
         .name = "phase",
         .type = 's',
         .dest = (void *)&phase,
         .descript = "provision, run, run-early or verify"},
        {.option = "workload",
         .name = "W",
         .type = 's',
         .dest = (void *)&workload,
         .descript = "run and verify: W1, W2, W3 or W4"},
        {.option = "acked",
         .name = "n",
         .type = 'u',
         .dest = (void *)&acked,
         .descript = "verify: steps acknowledged before the cut"},
        {.option = "acked-only",
         .is_switch = true,
         .type = 'b',
         .dest = (void *)&acked_only,
         .descript = "verify: W1 data checks only the acknowledged records"},
        ARG_TABLE_ENDMARKER,
    };

    native_add_command_line_opts(options);
}
NATIVE_TASK(add_options, PRE_BOOT_1, 10);

static void finish(int code, const char *text)
{
    printf("%s\n", text);
    fflush(stdout);
    posix_exit(code);
}

static void fail(const char *what, int rc)
{
    printf("FAIL %s %d\n", what, rc);
    fflush(stdout);
    posix_exit(1);
}

static bool phase_is(const char *name)
{
    return phase != NULL && strcmp(phase, name) == 0;
}

static bool parse_workload(enum powercut_workload *out)
{
    static const char *const names[] = {"W1", "W2", "W3", "W4"};

    for (size_t i = 0; workload != NULL && i < ARRAY_SIZE(names); i++) {
        if (strcmp(workload, names[i]) == 0) {
            *out = (enum powercut_workload)i;
            return true;
        }
    }
    return false;
}

static void provision(void)
{
    int rc = powercut_fs_format();

    if (rc != 0) {
        fail("format", rc);
    }
    rc = powercut_fs_mount();
    if (rc != 0) {
        fail("mount", rc);
    }
    for (int w = POWERCUT_W1; w <= POWERCUT_W3; w++) {
        rc = powercut_prepare((enum powercut_workload)w);
        if (rc != 0) {
            fail("prepare", rc);
        }
    }
    rc = powercut_fs_unmount();
    if (rc != 0) {
        fail("unmount", rc);
    }
    finish(0, "PROVISIONED");
}

static void run(enum powercut_workload w, bool early_ack)
{
    int rc = powercut_fs_mount();

    if (rc != 0) {
        fail("mount", rc);
    }
    rc = powercut_run(w, early_ack);
    if (rc != 0) {
        fail("run", rc);
    }
    rc = powercut_fs_unmount();
    if (rc != 0) {
        fail("unmount", rc);
    }
    finish(0, "RAN");
}

static void verify(enum powercut_workload w)
{
    struct powercut_result result;

    powercut_set_acked_only(acked_only);
    powercut_verify(w, acked, &result);
    printf("POWERCUT mount=%d data=%d read_error=%d replace=%d untouched=%d recommit=%d\n",
           result.mounted, result.data, result.read_error, result.replaced, result.untouched,
           result.recommit);
    fflush(stdout);
    posix_exit(0);
}

int main(void)
{
    enum powercut_workload w;

    if (phase_is("provision")) {
        provision();
    }
    if (!parse_workload(&w)) {
        finish(64, "FAIL usage: --workload=W1|W2|W3|W4 is required");
    }
    if (phase_is("run") || phase_is("run-early")) {
        run(w, phase_is("run-early"));
    }
    /* The parser leaves an unsigned option that was not given at UINT32_MAX. */
    if (phase_is("verify") && acked != UINT32_MAX) {
        verify(w);
    }
    finish(64, "FAIL usage: --phase=provision|run|run-early|verify, and verify needs --acked");
    return 0;
}
