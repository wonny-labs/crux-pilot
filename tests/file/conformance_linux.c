#include "conformance.h"
#include "file_linux.h"
#include "file_port.h"
#include <assert.h>
#include <dirent.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static char root_paths[2][64];

static size_t descriptor_count(void)
{
    DIR *directory = opendir("/proc/self/fd");
    assert(directory);
    size_t count = 0;
    while (readdir(directory)) {
        count++;
    }
    assert(closedir(directory) == 0);
    return count;
}

static void add_directory(crux_file_root_t root, const char *name)
{
    char path[128];
    assert(snprintf(path, sizeof(path), "%s/%s", root_paths[root], name) > 0);
    assert(mkdir(path, 0700) == 0);
}

static void add_file(crux_file_root_t root, const char *name)
{
    char path[256];
    assert(snprintf(path, sizeof(path), "%s/%s", root_paths[root], name) > 0);
    int fd = open(path, O_CREAT | O_WRONLY | O_CLOEXEC, 0600);
    assert(fd >= 0);
    assert(close(fd) == 0);
}

static void add_foreign_files(crux_file_root_t root)
{
    char too_long[65];
    memset(too_long, 'y', sizeof(too_long) - 1);
    too_long[sizeof(too_long) - 1] = '\0';
    add_file(root, "has space");
    add_file(root, too_long);

    /* The provider's open refuses to follow a link, so the listing does not report one. */
    char path[128];
    assert(snprintf(path, sizeof(path), "%s/link", root_paths[root]) > 0);
    assert(symlink("/etc/passwd", path) == 0);
}

int main(void)
{
    char base[] = "/tmp/crux-file-roots-XXXXXX";
    assert(mkdtemp(base));
    char parent[64], child[96], free_path[64], prefix[64], b[64];
    assert(snprintf(parent, sizeof(parent), "%s/volume", base) > 0);
    assert(mkdir(parent, 0700) == 0);
    assert(snprintf(root_paths[0], sizeof(root_paths[0]), "%s/a", parent) > 0);
    assert(mkdir(root_paths[0], 0700) == 0);
    assert(snprintf(child, sizeof(child), "%s/inner", root_paths[0]) > 0);
    assert(snprintf(b, sizeof(b), "%s/b", parent) > 0);
    assert(mkdir(b, 0700) == 0);
    memcpy(root_paths[1], b, sizeof(root_paths[1]));
    assert(snprintf(free_path, sizeof(free_path), "%s/free", base) > 0);
    assert(mkdir(free_path, 0700) == 0);
    assert(snprintf(prefix, sizeof(prefix), "%s/a2", base) > 0);

    /* Only path text is compared, so this one is written relative to the same parent as root 0. */
    char prefix_of_0[64];
    assert(snprintf(prefix_of_0, sizeof(prefix_of_0), "%s2", root_paths[0]) > 0);
    assert(mkdir(prefix_of_0, 0700) == 0);

    assert(crux_file_linux_bind_root(0, root_paths[0]) == CRUX_FILE_OK);
    assert(crux_file_linux_bind_root(1, root_paths[1]) == CRUX_FILE_OK);
    /* A trailing slash is the same directory. */
    char slashed[72];
    assert(snprintf(slashed, sizeof(slashed), "%s/", root_paths[0]) > 0);
    assert(crux_file_linux_bind_root(2, slashed) == CRUX_FILE_ERR_INVALID_STATE);
    assert(crux_file_linux_bind(root_paths[1]) == CRUX_FILE_ERR_INVALID_STATE);

    assert(mkdir(child, 0700) == 0);
    const crux_file_conformance_env_t env = {
        .add_directory = add_directory,
        .add_foreign_files = add_foreign_files,
        .bind_root = crux_file_linux_bind_root,
        .path_0 = root_paths[0],
        .path_0_parent = parent,
        .path_0_child = child,
        .path_free = free_path,
        .path_0_prefix = prefix_of_0,
    };

    size_t descriptors = descriptor_count();
    crux_file_conformance_roots(&env);
    crux_file_conformance_stat(&env);
    crux_file_conformance_size();
    crux_file_conformance_rename(&env);
    crux_file_conformance_listing(&env);
    assert(descriptor_count() == descriptors);

    /* A link is not a regular file to stat or rename, and a link is an occupied destination. */
    crux_file_info_t info;
    assert(crux_file_stat(1, "link", &info) == CRUX_FILE_ERR_NOT_FOUND);
    assert(crux_file_rename(1, "link", "moved") == CRUX_FILE_ERR_NOT_FOUND);
    crux_file_t *file = NULL;
    assert(crux_file_open_at(1, "real", CRUX_FILE_CREATE_REPLACE, &file) == CRUX_FILE_OK);
    assert(crux_file_close(&file) == CRUX_FILE_OK);
    assert(crux_file_rename(1, "real", "link") == CRUX_FILE_ERR_EXISTS);
    assert(crux_file_remove_at(1, "real") == CRUX_FILE_OK);

    assert(crux_file_linux_last_cleanup_error() == CRUX_FILE_OK);
    /* The top of the tree is a parent of everything. */
    assert(crux_file_linux_bind_root(2, "/") == CRUX_FILE_ERR_INVALID_STATE);
    crux_file_conformance_binding(&env);
    puts("File conformance on the Linux provider passed");
    return 0;
}
