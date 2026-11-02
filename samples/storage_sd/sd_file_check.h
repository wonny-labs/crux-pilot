/* SPDX-License-Identifier: Apache-2.0 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

/* Physical-gate check for the microSD composition: through crux_file_* it advances a boot counter
 * and verifies a fixed pattern file, so a restart shows what the previous boot left on the card.
 * Project code, not a Crux API. It writes to the card: use it only when the physical gate is
 * approved for the current task. */

#define SD_FILE_CHECK_COUNTER "bootcount"
#define SD_FILE_CHECK_PATTERN "pattern"
#define SD_FILE_CHECK_PATTERN_BYTES 4096

enum sd_file_check_status {
    SD_FILE_CHECK_OK = 0,
    SD_FILE_CHECK_COUNTER_IO,
    SD_FILE_CHECK_COUNTER_INVALID, /* present but not a decimal number; left unchanged */
    SD_FILE_CHECK_PATTERN_IO,
    SD_FILE_CHECK_PATTERN_MISMATCH, /* wrong length or a differing byte */
};

struct sd_file_check {
    uint32_t boot_count;  /* value written by this run (valid once the counter step succeeded) */
    bool pattern_created; /* the pattern file was missing and was written by this run */
};

/* Read the counter (missing means 0), write it back increased and synced, then verify the pattern
 * file, creating and re-reading it when it does not exist. Stops at the first failure. */
enum sd_file_check_status sd_file_check_run(struct sd_file_check *check);
