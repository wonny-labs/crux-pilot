/* How a reboot case runs on native_sim. On the Host every case runs in a forked child, so that each
 * starts from nothing: the service has no stop, and its state, its worker and its Sync objects stay
 * for the life of the process. Zephyr has no fork(), so here the image runs one case for each process.
 *
 *   zephyr.exe -test=<suite>::<name> -reboot-list         prints "CASE <index> <name>" for each case
 *   zephyr.exe -test=<suite>::<name> -reboot-case=<index> runs that one case
 *
 * scripts/test-reboot-native.sh starts the image once for each case, which is what the fork did. A run
 * with neither option fails: the second case in one process would run on the first one's state. */
#include "reboot_test.h"

#include "main_common.h"

#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>
#include <zephyr/ztest.h>

#include <limits.h>

#include "cmdline.h"
#include "soc.h"

/* The command line parser leaves an integer option it was not given at INT_MAX. */
#define NOT_SELECTED INT_MAX
static int selected = NOT_SELECTED;
static bool list_only;
static int next_index;
static int cases_run;

static void add_options(void)
{
    static struct args_struct_t options[] = {
        {false, false, true, "reboot-list", NULL, 'b', (void *)&list_only, NULL,
         "List the cases of the suite that is run"},
        {false, false, false, "reboot-case", "index", 'i', (void *)&selected, NULL,
         "Run only this case of the suite, counted from 0"},
        ARG_TABLE_ENDMARKER};

    native_add_command_line_opts(options);
}

NATIVE_TASK(add_options, PRE_BOOT_1, 10);

void reboot_native_expect_one_case(const char *suite)
{
    if (list_only) {
        printk("SUITE %s CASES %d\n", suite, next_index);
        return;
    }
    zassert_true(selected != NOT_SELECTED,
                 "%s: pass -reboot-case=<index>, one case for each process (see zephyr_case.c)", suite);
    zassert_true(selected < next_index, "%s: no case %d, the suite has %d", suite, selected,
                 next_index);
    zassert_equal(cases_run, 1, "%s: %d cases ran, expected one", suite, cases_run);
    next_index = 0;
}

void reboot_run_case(const char *name, void (*body)(void))
{
    const int index = next_index++;

    if (list_only) {
        printk("CASE %d %s\n", index, name);
        return;
    }
    if (index != selected) {
        return;
    }
    printk("RUN %d %s\n", index, name);
    body();
    ++cases_run;
}

int reboot_finish(const char *suite)
{
    printk("%s: case %d ran, %u checks, %u failures\n", suite, selected, access_test_checks,
           access_test_failures);
    return access_test_failures == 0u ? 0 : 1;
}
