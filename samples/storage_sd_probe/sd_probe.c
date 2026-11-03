/* SPDX-License-Identifier: Apache-2.0 */
#include "sd_probe.h"
#include "sd_storage.h"
#include <crux/file.h>
#include <string.h>
#include <zephyr/fs/fs.h>

#define PROBE_A SD_STORAGE_SETTINGS_DIR "/probe.a"
#define PROBE_B SD_STORAGE_SETTINGS_DIR "/probe.b"

enum { CHUNK = 512, CHUNKS = 128 /* 64 KiB: crosses several clusters */ };

static int fail(int code)
{
    return code != 0 ? code : -1;
}

int sd_probe_workload(void)
{
    struct fs_file_t file;
    struct fs_dir_t dir;
    struct fs_dirent entry;
    uint8_t buffer[CHUNK];
    int rc;

    memset(buffer, 0xA5, sizeof(buffer));
    fs_file_t_init(&file);
    rc = fs_open(&file, PROBE_A, FS_O_CREATE | FS_O_RDWR | FS_O_TRUNC);
    if (rc != 0) {
        return fail(rc);
    }
    for (int i = 0; i < CHUNKS && rc == 0; i++) {
        rc = fs_write(&file, buffer, sizeof(buffer)) == (ssize_t)sizeof(buffer) ? 0 : -1;
    }
    if (rc == 0) {
        rc = fs_sync(&file);
    }
    if (rc == 0) {
        rc = fs_seek(&file, 0, FS_SEEK_SET);
    }
    for (int i = 0; i < CHUNKS && rc == 0; i++) {
        rc = fs_read(&file, buffer, sizeof(buffer)) == (ssize_t)sizeof(buffer) ? 0 : -1;
    }
    int closed = fs_close(&file);
    if (rc != 0 || closed != 0) {
        return fail(rc != 0 ? rc : closed);
    }
    rc = fs_rename(PROBE_A, PROBE_B);
    if (rc == 0) {
        rc = fs_stat(PROBE_B, &entry);
    }
    if (rc == 0) {
        fs_dir_t_init(&dir);
        rc = fs_opendir(&dir, SD_STORAGE_MOUNT);
        while (rc == 0 && fs_readdir(&dir, &entry) == 0 && entry.name[0] != '\0') {
        }
        (void)fs_closedir(&dir);
    }
    int removed = fs_unlink(PROBE_B);
    return rc != 0 ? fail(rc) : (removed != 0 ? fail(removed) : 0);
}

void sd_probe_init(struct sd_probe *probe)
{
    memset(probe, 0, sizeof(*probe));
}

static int operation(int codes[4])
{
    crux_file_t *file = NULL;
    uint8_t record[32] = {0};
    size_t count = 0;

    memset(codes, 0, 4 * sizeof(int));
    codes[0] = crux_file_open("probe", CRUX_FILE_CREATE_REPLACE, &file);
    if (codes[0] != CRUX_FILE_OK) {
        return codes[0];
    }
    codes[1] = crux_file_write(file, record, sizeof(record), &count);
    codes[2] = crux_file_sync(file);
    codes[3] = crux_file_close(&file);
    for (int i = 1; i < 4; i++) {
        if (codes[i] != CRUX_FILE_OK) {
            return codes[i];
        }
    }
    return count == sizeof(record) ? 0 : -1;
}

void sd_probe_step(struct sd_probe *probe)
{
    if (probe->state == SD_PROBE_LOST) {
        probe->remount = sd_storage_remount();
        if (probe->remount == 0) {
            probe->state = SD_PROBE_RECOVERED;
        }
        return;
    }
    probe->ops++;
    if (operation(probe->codes) == 0) {
        if (probe->state == SD_PROBE_RECOVERED) {
            probe->recoveries++;
        }
        probe->state = SD_PROBE_OK;
        return;
    }
    probe->failures++;
    probe->state = SD_PROBE_LOST;
    (void)sd_storage_stop();
}
