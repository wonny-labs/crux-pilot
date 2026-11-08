/* SPDX-License-Identifier: Apache-2.0 */
#include "sd_settings_recover.h"
#include <errno.h>
#include <string.h>
#include <zephyr/fs/fs.h>

/* The backend names its temporary "<file>.cmp" and cuts the name at 31 bytes (its
 * SETTINGS_FILE_NAME_MAX of 32 is private), so a longer path has a different temporary name. */
enum { SUFFIX_LEN = 4, NAME_LIMIT = 32 };

int sd_settings_recover(const char *file)
{
    char tmp[NAME_LIMIT];
    struct fs_dirent entry;
    size_t len = strlen(file);
    int rc;

    if (len + SUFFIX_LEN >= NAME_LIMIT) {
        return -ENAMETOOLONG;
    }
    memcpy(tmp, file, len);
    memcpy(tmp + len, ".cmp", SUFFIX_LEN + 1);

    rc = fs_stat(file, &entry);
    if (rc == 0) {
        return entry.type == FS_DIR_ENTRY_FILE ? 0 : -EISDIR;
    }
    if (rc != -ENOENT) {
        return rc; /* an unreadable directory entry is not a missing file */
    }
    rc = fs_stat(tmp, &entry);
    if (rc == -ENOENT) {
        return 0;
    }
    if (rc != 0) {
        return rc;
    }
    if (entry.type != FS_DIR_ENTRY_FILE) {
        return -EISDIR;
    }
    if (entry.size == 0) {
        return -ENODATA; /* a finished compaction always holds at least the new record */
    }
    rc = fs_rename(tmp, file);
    return rc == 0 ? SD_SETTINGS_RECOVERED : rc;
}
