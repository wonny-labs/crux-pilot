/*
 * The mount and format decision of the bench image's file medium (row C3, D2). No Zephyr header
 * here: tests/zephyr/management_rt1060_compile/host/test_bench_medium.c runs this file against fake
 * devices. See bench_medium.h for the rule.
 */
#include "bench_medium.h"
#include "bench_guard.h"

bench_medium_result_t bench_medium_prepare(bench_medium_kind_t kind, const bench_medium_ops_t *ops)
{
    if ((kind != BENCH_MEDIUM_RAM_DISK && kind != BENCH_MEDIUM_CARD) || ops == NULL ||
        ops->mount == NULL) {
        return BENCH_MEDIUM_INVALID;
    }
    if (ops->mount(ops->context) == 0) {
        return BENCH_MEDIUM_MOUNTED;
    }
    /* The one place a format is reached. A card falls through to the report below. */
    if (kind == BENCH_MEDIUM_RAM_DISK) {
        if (ops->format == NULL || ops->format(ops->context) != 0) {
            return BENCH_MEDIUM_FORMAT_FAILED;
        }
        return ops->mount(ops->context) == 0 ? BENCH_MEDIUM_FORMATTED_AND_MOUNTED
                                             : BENCH_MEDIUM_MOUNT_FAILED_AFTER_FORMAT;
    }
    return BENCH_MEDIUM_NOT_MOUNTED_NOT_FORMATTED;
}
