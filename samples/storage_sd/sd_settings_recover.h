/* SPDX-License-Identifier: Apache-2.0 */
#pragma once

/* One recovery step for the Settings File backend on FatFs. Project code, not a Crux API.
 *
 * The backend compacts by writing "<file>.cmp", closing it, then calling fs_rename(), and FatFs
 * removes the destination before it renames. A power cut between the two leaves no settings file
 * and a complete "<file>.cmp": the temporary is closed (and so synced) before the rename starts.
 * Call this once after the volume is mounted and before the first settings load; it completes the
 * rename in that one state and does nothing in any other. */

#define SD_SETTINGS_RECOVERED 1

/* "file" is the backend's file path (CONFIG_SETTINGS_FILE_PATH). Returns:
 *   0                 nothing to do (the file exists, or there is no temporary)
 *   SD_SETTINGS_RECOVERED (1)  the temporary was renamed to the settings file
 *   -ENODATA          no file and the temporary is empty: not a finished compaction, left alone
 *   -EISDIR           the file or the temporary is a directory; nothing is changed
 *   -ENAMETOOLONG     the path plus ".cmp" does not fit the backend's 32-byte name limit, so the
 *                     backend's temporary name cannot be derived; nothing is changed
 *   other errno       a stat or the rename failed; a stat error is never read as "missing" */
int sd_settings_recover(const char *file);
