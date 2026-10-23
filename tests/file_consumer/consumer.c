#include "consumer.h"
#include <crux/file.h>
#include <string.h>
int file_consumer(void) {
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
    return result;
}
