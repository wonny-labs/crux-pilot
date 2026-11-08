/*
 * Placeholder of the bench image's first account (D3). The native images create their first account
 * with a password that is public in this repository, and the bench image must not: the vehicle's
 * copy of crux_native_management.c has no such step. Row C4 of
 * docs/rt1060-management-image-feasibility.md replaces this file with a step that reads the
 * password from an untracked file, hashes it into the manifest, and never prints it, and then
 * changes the state of the test form in
 * ../bench_declaration.json.
 */
#include "bench_first_account.h"

#include "bench_guard.h"

#include <zephyr/sys/printk.h>

bool crux_bench_first_account(void)
{
    printk("BENCH first account: not implemented (row C4), the image does not serve\n");
    return false;
}
