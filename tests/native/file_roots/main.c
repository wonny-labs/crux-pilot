#include "conformance.h"
#include "file_native.h"
#include "file_port.h"
#include <crux/file.h>
#include <ff.h>
#include <stdio.h>
#include <string.h>
#include <zephyr/fs/fs.h>
#include <zephyr/ztest.h>

/* The File additions on real Zephyr FatFs over a RAM disk: not a card, and not power-loss evidence.
 */
static FATFS fat;
static struct fs_mount_t mount = {
    .type = FS_FATFS,
    .mnt_point = "/RAM:",
    .fs_data = &fat,
    .flags = FS_MOUNT_FLAG_NO_FORMAT,
};

static const char *const root_paths[] = {"/RAM:/a", "/RAM:/b"};

/* The provider's own encoding of a File name, so that a test can reach it from the VFS. */
static void encoded_path(char *path, size_t capacity, crux_file_root_t root, const char *name)
{
    size_t pos = (size_t)snprintf(path, capacity, "%s/f", root_paths[root]);
    for (const char *p = name; *p; p++) {
        pos += (size_t)snprintf(&path[pos], capacity - pos, "%02x", (unsigned char)*p);
    }
}

static void raw_file(crux_file_root_t root, const char *leaf)
{
    char path[160];
    struct fs_file_t file;
    fs_file_t_init(&file);
    zassert_true(snprintf(path, sizeof(path), "%s/%s", root_paths[root], leaf) < sizeof(path));
    zassert_ok(fs_open(&file, path, FS_O_CREATE | FS_O_WRITE), "%s", leaf);
    zassert_ok(fs_close(&file));
}

static void add_directory(crux_file_root_t root, const char *name)
{
    char path[160];
    encoded_path(path, sizeof(path), root, name);
    zassert_ok(fs_mkdir(path));
}

static void add_foreign_files(crux_file_root_t root)
{
    raw_file(root, "notes.txt");
    raw_file(root, "with space");
    raw_file(root, "f");       /* An empty name. */
    raw_file(root, "f6");      /* An odd digit count. */
    raw_file(root, "f6g");     /* Not hexadecimal. */
    raw_file(root, "f6A");     /* Upper case, so not this provider's encoding of "j". */
    raw_file(root, "f6100");   /* A NUL inside, which would otherwise alias "a". */
    raw_file(root, "f612f62"); /* Decodes to "a/b", which fails the name rule. */
    raw_file(root, "f2e2e");   /* Decodes to "..". */
}

static const crux_file_conformance_env_t env = {
    .add_directory = add_directory,
    .add_foreign_files = add_foreign_files,
    .bind_root = crux_file_zephyr_bind_root,
    .path_0 = "/RAM:/a",
    .path_0_parent = "/RAM:",
    .path_0_child = "/RAM:/a/inner",
    .path_free = "/RAM:/free",
    .path_0_prefix = "/RAM:/a2",
};

static void *setup(void)
{
    zassert_ok(fs_mkfs(FS_FATFS, (uintptr_t) "RAM:", NULL, 0));
    zassert_ok(fs_mount(&mount));
    zassert_ok(fs_mkdir("/RAM:/a"));
    zassert_ok(fs_mkdir("/RAM:/a/inner"));
    zassert_ok(fs_mkdir("/RAM:/b"));
    zassert_ok(fs_mkdir("/RAM:/free"));
    zassert_ok(fs_mkdir("/RAM:/a2"));
    zassert_equal(crux_file_zephyr_bind("/RAM:/a"), CRUX_FILE_OK, "the original bind is root 0");
    zassert_equal(crux_file_zephyr_bind_root(1, "/RAM:/b"), CRUX_FILE_OK);
    return NULL;
}

/* One case, because the conformance sequence leaves entries that its later steps expect. */
ZTEST(file_roots, test_conformance_on_fatfs)
{
    crux_file_conformance_roots(&env);
    crux_file_conformance_stat(&env);
    crux_file_conformance_size();
    crux_file_conformance_rename(&env);
    crux_file_conformance_listing(&env);
    crux_file_conformance_binding(&env);
}

ZTEST(file_roots, test_a_rename_survives_a_remount_and_never_leaves_two_names)
{
    crux_file_t *file = NULL;
    size_t count = 0;
    zassert_equal(crux_file_open_at(1, "before", CRUX_FILE_CREATE_REPLACE, &file), CRUX_FILE_OK);
    zassert_equal(crux_file_write(file, "data", 4, &count), CRUX_FILE_OK);
    zassert_equal(crux_file_sync(file), CRUX_FILE_OK);
    zassert_equal(crux_file_close(&file), CRUX_FILE_OK);
    zassert_equal(crux_file_rename(1, "before", "after"), CRUX_FILE_OK);

    /* The same in-process RAM disk, so this is not persistence evidence. */
    zassert_ok(fs_unmount(&mount));
    zassert_ok(fs_mount(&mount));
    crux_file_info_t info;
    zassert_equal(crux_file_stat(1, "before", &info), CRUX_FILE_ERR_NOT_FOUND);
    zassert_equal(crux_file_stat(1, "after", &info), CRUX_FILE_OK);
    zassert_equal(info.size, 4);
    zassert_equal(crux_file_remove_at(1, "after"), CRUX_FILE_OK);
}

/* The upstream fatfs_rename removes an existing destination, so this is the check that matters. */
ZTEST(file_roots, test_rename_keeps_an_existing_destination_on_fatfs)
{
    crux_file_t *file = NULL;
    size_t count = 0;
    zassert_equal(crux_file_open_at(1, "src", CRUX_FILE_CREATE_REPLACE, &file), CRUX_FILE_OK);
    zassert_equal(crux_file_close(&file), CRUX_FILE_OK);
    zassert_equal(crux_file_open_at(1, "dst", CRUX_FILE_CREATE_REPLACE, &file), CRUX_FILE_OK);
    zassert_equal(crux_file_write(file, "kept", 4, &count), CRUX_FILE_OK);
    zassert_equal(crux_file_close(&file), CRUX_FILE_OK);

    zassert_equal(crux_file_rename(1, "src", "dst"), CRUX_FILE_ERR_EXISTS);

    crux_file_info_t info;
    zassert_equal(crux_file_stat(1, "dst", &info), CRUX_FILE_OK);
    zassert_equal(info.size, 4);
    zassert_equal(crux_file_stat(1, "src", &info), CRUX_FILE_OK);
    zassert_equal(crux_file_remove_at(1, "src"), CRUX_FILE_OK);
    zassert_equal(crux_file_remove_at(1, "dst"), CRUX_FILE_OK);
}

/* Short encoded names are the ones FatFs may store as 8.3, so they prove the listing's case. */
ZTEST(file_roots, test_listing_decodes_short_and_long_encoded_names)
{
    const char *names[] = {"a", "z.b", "abc", "0123456789abcdefghij"};
    crux_file_t *file = NULL;
    for (size_t i = 0; i < ARRAY_SIZE(names); i++) {
        zassert_equal(crux_file_open_at(1, names[i], CRUX_FILE_CREATE_REPLACE, &file),
                      CRUX_FILE_OK);
        zassert_equal(crux_file_close(&file), CRUX_FILE_OK);
    }

    crux_file_dir_t *dir = NULL;
    char name[64];
    crux_file_info_t info;
    bool end = false;
    bool seen[ARRAY_SIZE(names)] = {false};
    zassert_equal(crux_file_dir_open(1, &dir), CRUX_FILE_OK);
    for (;;) {
        zassert_equal(crux_file_dir_read(dir, name, sizeof(name), &info, &end), CRUX_FILE_OK);
        if (end) {
            break;
        }
        for (size_t i = 0; i < ARRAY_SIZE(names); i++) {
            seen[i] = seen[i] || strcmp(name, names[i]) == 0;
        }
    }
    zassert_equal(crux_file_dir_close(&dir), CRUX_FILE_OK);
    for (size_t i = 0; i < ARRAY_SIZE(names); i++) {
        zassert_true(seen[i], "%s was not listed", names[i]);
        zassert_equal(crux_file_remove_at(1, names[i]), CRUX_FILE_OK);
    }
}

ZTEST(file_roots, test_an_unmounted_volume_is_not_ready)
{
    crux_file_info_t info;
    crux_file_dir_t *dir = NULL;
    zassert_ok(fs_unmount(&mount));
    zassert_equal(crux_file_stat(1, "x", &info), CRUX_FILE_ERR_NOT_READY);
    zassert_equal(crux_file_rename(1, "x", "y"), CRUX_FILE_ERR_NOT_READY);
    zassert_equal(crux_file_dir_open(1, &dir), CRUX_FILE_ERR_NOT_READY);
    zassert_is_null(dir);
    zassert_ok(fs_mount(&mount));
}

ZTEST_SUITE(file_roots, NULL, setup, NULL, NULL, NULL);
