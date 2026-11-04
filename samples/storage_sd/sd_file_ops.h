/* SPDX-License-Identifier: Apache-2.0 */
#pragma once

/* Physical-gate check for the microSD composition: through crux_file_* it creates a scratch file,
 * seeks inside it and to its end, reads back, removes it and checks that it is gone. It covers the
 * seek and remove calls that sd_file_check does not use. Project code, not a Crux API. It writes
 * to the card: use it only when the physical gate is approved for the current task. */

#define SD_FILE_OPS_NAME "opscheck"
#define SD_FILE_OPS_BYTES 256

enum sd_file_ops_status {
    SD_FILE_OPS_OK = 0,
    SD_FILE_OPS_CREATE_FAILED,   /* open, write, sync or close of the scratch file failed */
    SD_FILE_OPS_SEEK_FAILED,     /* open for reading, seek, read or close failed */
    SD_FILE_OPS_SEEK_MISMATCH,   /* bytes read after a seek differ from what was written */
    SD_FILE_OPS_REMOVE_FAILED,   /* the first remove did not succeed */
    SD_FILE_OPS_STILL_PRESENT,   /* the file can still be opened after the remove */
    SD_FILE_OPS_REMOVE_AGAIN_OK, /* removing a missing file did not report NOT_FOUND */
};

/* Replaces any leftover scratch file, so a run is repeatable. Stops at the first failure; on
 * failure the scratch file may remain on the card. */
enum sd_file_ops_status sd_file_ops_run(void);
