#include <crux/file.h>
crux_file_result_t pilot_file_create(void) {
    crux_file_t *file = 0;
    crux_file_result_t rc = crux_file_open("portable", CRUX_FILE_CREATE_REPLACE, &file);
    if (rc != CRUX_FILE_OK)
        return rc;
    size_t n = 0;
    rc = crux_file_write(file, "consumer", 8, &n);
    if (rc == CRUX_FILE_OK && n == 8)
        rc = crux_file_sync(file);
    crux_file_result_t closed = crux_file_close(&file);
    return rc == CRUX_FILE_OK ? closed : rc;
}
