/* SPDX-License-Identifier: Apache-2.0 */
#pragma once

#include <crux/file.h>
#include <stdbool.h>
#include <stdint.h>

/* Interrupted-write test for the microSD composition. Every boot advances a counter, reports what
 * the previous boot's run file holds after a cut, and then appends fixed 32-byte records one by
 * one, each synced before the host sees "ACK <seq>". The host pairs the last ACK it saw before a
 * power cut with the next boot's last_valid. Project code, not a Crux API. It writes to the card
 * and is meant to be interrupted: use it only when the physical gate is approved for the current
 * task. */

#define SD_INTERRUPT_COUNTER "intcount"
#define SD_INTERRUPT_RECORD_BYTES 32

enum sd_interrupt_status {
    SD_INTERRUPT_OK = 0,
    SD_INTERRUPT_IO,
    SD_INTERRUPT_COUNTER_INVALID, /* present but not a decimal number; nothing was written */
};

struct sd_interrupt_recovery {
    uint32_t boot; /* this boot's number (valid once the counter step succeeded) */
    bool previous_run_present;
    uint32_t last_valid; /* records 1..last_valid of the previous run are intact */
    uint32_t torn_bytes; /* bytes after the last valid record */
    bool damaged_record; /* those bytes include a full-size record that fails its checks */
};

/* Advance the counter, recover the previous boot's run file ("run<boot-1>"), and open a new run
 * file "run<boot>" for appending. On any failure *run is left null. */
enum sd_interrupt_status sd_interrupt_begin(struct sd_interrupt_recovery *recovery,
                                            crux_file_t **run);

/* One-byte checksum of a console line's text, printed as ` *XX` after BOOT, RECOVER and ACK lines
 * so the host can drop lines garbled while power collapsed. The host script computes the same
 * value. */
uint8_t sd_interrupt_checksum(const char *text);

/* Write record seq and sync. The record is acknowledged only when this returns OK. */
enum sd_interrupt_status sd_interrupt_append(crux_file_t *run, uint32_t seq);

/* The bytes of record seq: magic, seq, a payload derived from seq, and a CRC-32. */
void sd_interrupt_make_record(uint32_t seq, uint8_t record[SD_INTERRUPT_RECORD_BYTES]);
