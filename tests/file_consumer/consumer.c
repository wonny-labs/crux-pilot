#include "consumer.h"
#include <crux/file.h>
#include <string.h>
static int file_consumer_additions(void)
{
    crux_file_t *f = NULL;
    crux_file_dir_t *d = NULL;
    crux_file_info_t info = {0};
    uint64_t size = 0;
    char name[64];
    bool end = false;
    if (crux_file_open_at(CRUX_FILE_ROOT_DEFAULT, "listed", CRUX_FILE_CREATE_REPLACE, &f) !=
        CRUX_FILE_OK)
        return 20;
    int result = 0;
    if (crux_file_size(f, &size) != CRUX_FILE_OK || size != 0)
        result = 21;
    if (crux_file_close(&f) != CRUX_FILE_OK && !result)
        result = 22;
    if (!result && crux_file_stat(CRUX_FILE_ROOT_DEFAULT, "listed", &info) != CRUX_FILE_OK)
        result = 23;
    if (!result && crux_file_rename(CRUX_FILE_ROOT_DEFAULT, "listed", "moved") != CRUX_FILE_OK)
        result = 24;
    if (!result &&
        crux_file_rename(CRUX_FILE_ROOT_DEFAULT, "moved", "moved") != CRUX_FILE_ERR_EXISTS)
        result = 25;
    if (!result && crux_file_dir_open(CRUX_FILE_ROOT_DEFAULT, &d) != CRUX_FILE_OK)
        result = 26;
    if (!result && (crux_file_dir_read(d, name, sizeof(name), &info, &end) != CRUX_FILE_OK || end ||
                    strcmp(name, "moved") != 0))
        result = 27;
    if (d && crux_file_dir_close(&d) != CRUX_FILE_OK && !result)
        result = 28;
    if (crux_file_remove_at(CRUX_FILE_ROOT_DEFAULT, "moved") != CRUX_FILE_OK && !result)
        result = 29;
    return result;
}
int file_consumer(void)
{
    crux_file_t *f = NULL;
    size_t n = 0;
    char data[4] = {0};
    if (crux_file_open("records", CRUX_FILE_CREATE_REPLACE, &f) != CRUX_FILE_OK)
        return 1;
    int result = 0;
    if (crux_file_write(f, "one.two", 7, &n) != CRUX_FILE_OK || n != 7)
        result = 2;
    if (!result && crux_file_sync(f) != CRUX_FILE_OK)
        result = 3;
    if (!result && crux_file_seek(f, 4) != CRUX_FILE_OK)
        result = 4;
    if (!result &&
        (crux_file_read(f, data, 3, &n) != CRUX_FILE_OK || n != 3 || memcmp(data, "two", 3) != 0))
        result = 5;
    if (!result && crux_file_seek(f, 0) != CRUX_FILE_OK)
        result = 6;
    if (!result &&
        (crux_file_read(f, data, 3, &n) != CRUX_FILE_OK || n != 3 || memcmp(data, "one", 3) != 0))
        result = 7;
    if (crux_file_close(&f) != CRUX_FILE_OK && !result)
        result = 8;
    if (crux_file_remove("records") != CRUX_FILE_OK && !result)
        result = 9;
    if (!result) {
        result = file_consumer_additions();
    }
    return result;
}
