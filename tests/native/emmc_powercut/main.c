/* SPDX-License-Identifier: Apache-2.0 */
/*
 * One program, phases selected on the command line, run as separate processes by run_replay.py on
 * the eMMC simulator's host image.
 *   provision  format and write the baseline content of every workload
 *   run        run a workload, with a trace mark after each acknowledged step
 *   run-early  the same, with each mark before the step's sync: a deliberate defect for a control
 *   stage      W3 only, a test of the check: the state after --acked steps, with the W3 file
 *              --empty=INDEX, if given, left with size 0
 *   stage-w4   W4 only, a test of the check: --damage=missing|leftover|none on an image after a
 *              complete run (removes the settings file, adds a temporary beside it, or nothing)
 *   verify     mount without formatting, check the invariants and print one POWERCUT line;
 *              --acked-only narrows the W1 data check to the acknowledged records;
 *              --create-may-be-empty lets the W3 check accept a created, still empty file of the
 *              step in progress, and appends created_empty=0|1 to the line; for W4 the line ends
 *              with cmp=0|1, whether the Settings File backend's temporary is left behind
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
static bool create_may_be_empty;
static uint32_t empty_path;
static const char *damage;

static void add_options(void)
{
    static struct args_struct_t options[] = {
        {.option = "phase",
         .name = "phase",
         .type = 's',
         .dest = (void *)&phase,
         .descript = "provision, run, run-early, verify, stage or stage-w4"},
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
        {.option = "empty",
         .name = "index",
         .type = 'u',
         .dest = (void *)&empty_path,
         .descript = "stage: the W3 file to leave with size 0"},
        {.option = "damage",
         .name = "what",
         .type = 's',
         .dest = (void *)&damage,
         .descript = "stage-w4: missing, leftover or none"},
        {.option = "create-may-be-empty",
         .is_switch = true,
         .type = 'b',
         .dest = (void *)&create_may_be_empty,
         .descript = "verify: W3 data also accepts a file created and still empty by the step "
                     "after the acknowledged one; adds created_empty to the line"},
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
    /* W4 saves through the Settings File backend, so only an image with it has a W4 baseline. */
    if (IS_ENABLED(CONFIG_SETTINGS_FILE)) {
        rc = powercut_prepare(POWERCUT_W4);
        if (rc != 0) {
            fail("prepare W4", rc);
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

static void stage(void)
{
    int rc = powercut_fs_mount();

    if (rc != 0) {
        fail("mount", rc);
    }
    rc = powercut_stage_w3(acked, empty_path);
    if (rc != 0 && rc != POWERCUT_STAGE_SKIP) {
        fail("stage", rc);
    }
    if (powercut_fs_unmount() != 0) {
        fail("unmount", -1);
    }
    finish(0, rc == POWERCUT_STAGE_SKIP ? "STAGE-SKIP" : "STAGED");
}

static void stage_w4(void)
{
    int rc = powercut_fs_mount();

    if (rc != 0) {
        fail("mount", rc);
    }
    rc = powercut_stage_w4(damage != NULL ? damage : "");
    if (rc != 0) {
        fail("stage-w4", rc);
    }
    if (powercut_fs_unmount() != 0) {
        fail("unmount", -1);
    }
    finish(0, "STAGED");
}

static void verify(enum powercut_workload w)
{
    struct powercut_result result;

    powercut_set_acked_only(acked_only);
    powercut_set_create_may_be_empty(create_may_be_empty);
    powercut_verify(w, acked, &result);
    printf("POWERCUT mount=%d data=%d read_error=%d replace=%d untouched=%d recommit=%d",
           result.mounted, result.data, result.read_error, result.replaced, result.untouched,
           result.recommit);
    if (create_may_be_empty) {
        printf(" created_empty=%d", powercut_created_empty());
    }
    if (w == POWERCUT_W4) {
        printf(" cmp=%d", result.cmp_left);
    }
    printf("\n");
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
    if (phase_is("stage") && w == POWERCUT_W3 && acked != UINT32_MAX) {
        stage();
    }
    if (phase_is("stage-w4") && w == POWERCUT_W4 && damage != NULL) {
        stage_w4();
    }
    finish(64, "FAIL usage: --phase=provision|run|run-early|verify|stage|stage-w4, verify needs "
               "--acked, stage needs --workload=W3 and --acked, stage-w4 needs --workload=W4 and "
               "--damage");
    return 0;
}
