/* SPDX-License-Identifier: Apache-2.0 */
#pragma once

#include <stdint.h>

/* Replace-pattern power-cut test for the microSD composition. The Zephyr Settings File backend
 * compacts by writing "<name>.cmp" and calling fs_rename(tmp, name); FatFs's rename first removes
 * the destination, so a cut in between leaves no settings file. This image repeats that sequence
 * with the same fs_* calls on fixed 8-record files and classifies what a cut left behind. Project
 * code, not a Crux API. It writes to the card and is meant to be interrupted: use it only when the
 * physical gate is approved for the current task. */

#define SD_REPLACE_RECORD_BYTES 32
#define SD_REPLACE_RECORDS 8

enum sd_replace_status {
    SD_REPLACE_OK = 0,
    SD_REPLACE_IO = -1,
    SD_REPLACE_COUNTER_INVALID = -2, /* present but not a decimal number; nothing was classified */
};

enum sd_replace_state {
    SD_REPLACE_NONE,  /* the file does not exist */
    SD_REPLACE_VALID, /* complete, every record valid and of one generation */
    SD_REPLACE_BAD, /* exists but is short, long or has an invalid record; gen is that of the first
                       valid record, else 0 */
};

struct sd_replace_file {
    enum sd_replace_state state;
    uint32_t gen;
};

struct sd_replace_recovery {
    uint32_t boot;
    struct sd_replace_file cfg; /* the settings file "cfg" */
    struct sd_replace_file cmp; /* the temporary "cfg.cmp" */
};

/* Advance the boot counter (synced) and classify "cfg" and "cfg.cmp" in /SD:/settings. */
enum sd_replace_status sd_replace_begin(struct sd_replace_recovery *recovery);

/* One compaction: write generation gen into "cfg.cmp", sync, close, then fs_rename it over "cfg".
 */
enum sd_replace_status sd_replace_generation(uint32_t gen);

/* Write a complete file of generation gen to path (create or truncate), synced and closed. */
enum sd_replace_status sd_replace_write(const char *path, uint32_t gen);

/* The bytes of record index of generation gen: magic, gen, index, payload and a CRC-32. */
void sd_replace_make_record(uint32_t gen, uint32_t index, uint8_t record[SD_REPLACE_RECORD_BYTES]);
