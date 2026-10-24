#include "file_linux.h"
#include <assert.h>
#include <crux/file.h>
#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
static size_t descriptor_count(void) {
    DIR *directory = opendir("/proc/self/fd");
    assert(directory);
    size_t count = 0;
    while (readdir(directory))
        count++;
    assert(closedir(directory) == 0);
    return count;
}
int main(void) {
    crux_file_t *f = NULL;
    size_t n;
    char b[8] = {0};
    assert(crux_file_open("record", CRUX_FILE_READ, &f) == CRUX_FILE_ERR_NOT_READY);
    assert(crux_file_remove("record") == CRUX_FILE_ERR_NOT_READY);
    assert(crux_file_linux_bind(NULL) == CRUX_FILE_ERR_INVALID_ARGUMENT);
    assert(crux_file_linux_bind("/no/such/crux-root") == CRUX_FILE_ERR_NOT_FOUND);
    char root[] = "/tmp/crux-file-XXXXXX";
    assert(mkdtemp(root));
    assert(crux_file_linux_bind(root) == CRUX_FILE_OK);
    size_t initial_descriptors = descriptor_count();
    assert(crux_file_linux_last_cleanup_error() == CRUX_FILE_OK);
    char folder[128];
    assert(snprintf(folder, sizeof(folder), "%s/folder", root) > 0);
    assert(mkdir(folder, 0700) == 0);
    for (int i = 0; i < 20; i++)
        assert(crux_file_open("folder", CRUX_FILE_READ, &f) == CRUX_FILE_ERR_NOT_SUPPORTED && !f);
    assert(rmdir(folder) == 0);
    assert(crux_file_linux_bind(root) == CRUX_FILE_ERR_INVALID_STATE);
    assert(crux_file_open("missing", CRUX_FILE_READ, &f) == CRUX_FILE_ERR_NOT_FOUND && !f);
    assert(crux_file_open("record", CRUX_FILE_CREATE_REPLACE, &f) == CRUX_FILE_OK);
    assert(crux_file_write(f, "one.two", 7, &n) == CRUX_FILE_OK && n == 7);
    assert(crux_file_sync(f) == CRUX_FILE_OK);
    assert(crux_file_seek(f, 4) == CRUX_FILE_OK);
    assert(crux_file_read(f, b, 3, &n) == CRUX_FILE_OK && n == 3 && memcmp(b, "two", 3) == 0);
    assert(crux_file_seek(f, 0) == CRUX_FILE_OK);
    assert(crux_file_read(f, b, 3, &n) == CRUX_FILE_OK && n == 3 && memcmp(b, "one", 3) == 0);
    assert(crux_file_seek(f, 8) == CRUX_FILE_ERR_OUT_OF_RANGE);
    assert(crux_file_seek(f, UINT64_MAX) == CRUX_FILE_ERR_OUT_OF_RANGE);
    assert(crux_file_seek(f, 7) == CRUX_FILE_OK);
    assert(crux_file_read(f, b, 8, &n) == CRUX_FILE_OK && n == 0);
    assert(crux_file_close(&f) == CRUX_FILE_OK && !f);
    assert(crux_file_open("record", CRUX_FILE_READ, &f) == CRUX_FILE_OK);
    assert(crux_file_read(f, b, 8, &n) == CRUX_FILE_OK && n == 7 && memcmp(b, "one.two", 7) == 0);
    assert(crux_file_write(f, b, 1, &n) == CRUX_FILE_ERR_ACCESS && n == 0);
    assert(crux_file_close(&f) == CRUX_FILE_OK);
    assert(crux_file_open("record", CRUX_FILE_READ_WRITE, &f) == CRUX_FILE_OK);
    assert(crux_file_close(&f) == CRUX_FILE_OK);
    assert(crux_file_open("record", CRUX_FILE_CREATE_REPLACE, &f) == CRUX_FILE_OK);
    assert(crux_file_read(f, b, 8, &n) == CRUX_FILE_OK && n == 0);
    assert(crux_file_close(&f) == CRUX_FILE_OK);
    assert(crux_file_remove("record") == CRUX_FILE_OK);
    assert(crux_file_remove("record") == CRUX_FILE_ERR_NOT_FOUND);
    for (int i = 0; i < 20; i++) {
        assert(crux_file_open("repeat", CRUX_FILE_CREATE_REPLACE, &f) == CRUX_FILE_OK);
        assert(crux_file_close(&f) == CRUX_FILE_OK);
    }
    assert(crux_file_remove("repeat") == CRUX_FILE_OK);
    assert(crux_file_linux_error(ENOSPC) == CRUX_FILE_ERR_NO_SPACE);
    assert(crux_file_linux_error(EMFILE) == CRUX_FILE_ERR_NO_RESOURCES);
    assert(crux_file_linux_error(EACCES) == CRUX_FILE_ERR_ACCESS);
    assert(crux_file_linux_error(EOVERFLOW) == CRUX_FILE_ERR_OUT_OF_RANGE);
    assert(crux_file_linux_error(EINVAL) == CRUX_FILE_ERR_INVALID_ARGUMENT);
    assert(crux_file_linux_error(123456) == CRUX_FILE_ERR_IO);
    assert(descriptor_count() == initial_descriptors);
    assert(rmdir(root) == 0);
    return 0;
}
