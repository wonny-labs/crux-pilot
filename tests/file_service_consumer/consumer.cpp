#include <crux/file_service.h>

/* Compiled but not linked: every public declaration must build from the portable headers alone. */
crux_file_service_result_t file_service_consumer_cpp(crux_access_session_t session);
crux_file_service_result_t file_service_consumer_cpp(crux_access_session_t session)
{
    static const crux_file_area_t area = {"data",
                                          CRUX_FILE_ROOT_DEFAULT,
                                          "data.read",
                                          "data.write",
                                          1024,
                                          4096,
                                          8,
                                          ".filepart",
                                          CRUX_FILE_SERVICE_MANAGED | CRUX_FILE_SERVICE_DIRECT};
    static const crux_file_service_config_t config = {&area, 1, NULL, 0, true};
    crux_file_service_result_t result = crux_file_service_start(&config);
    const crux_file_area_t *resolved = NULL;
    char name[64];
    if (result == CRUX_FILE_SERVICE_OK) {
        result = crux_file_service_resolve("/data/file", &resolved, name, sizeof(name));
    }
    size_t cursor = 0;
    bool writable = false;
    if (result == CRUX_FILE_SERVICE_OK) {
        result = crux_file_service_area_next(session, &cursor, &resolved, &writable);
    }
    crux_file_service_dir_t *dir = NULL;
    crux_file_info_t info = {0};
    bool end = false;
    if (result == CRUX_FILE_SERVICE_OK) {
        result = crux_file_service_dir_open(session, "data", &dir);
    }
    if (result == CRUX_FILE_SERVICE_OK) {
        result = crux_file_service_dir_read(session, dir, name, sizeof(name), &info, &end);
        (void)crux_file_service_dir_close(&dir);
    }
    if (result == CRUX_FILE_SERVICE_OK) {
        result = crux_file_service_stat(session, "data", "file", &info);
    }
    crux_file_service_transfer_t *transfer = NULL;
    size_t count = 0;
    uint64_t size = 0;
    if (result == CRUX_FILE_SERVICE_OK) {
        result = crux_file_service_open(session, "data", "file", CRUX_FILE_SERVICE_OPEN_MANAGED, 0,
                                        &transfer);
    }
    if (result == CRUX_FILE_SERVICE_OK) {
        result = crux_file_service_write(session, transfer, 0, "x", 1, &count);
        (void)crux_file_service_size(session, transfer, &size);
        (void)crux_file_service_read(session, transfer, 0, name, 1, &count);
        result = crux_file_service_commit(session, &transfer);
        (void)crux_file_service_close(&transfer);
    }
    if (result == CRUX_FILE_SERVICE_OK) {
        result = crux_file_service_rename(session, "data", "file", "moved");
    }
    if (result == CRUX_FILE_SERVICE_OK) {
        result = crux_file_service_remove(session, "data", "moved");
    }
    return result;
}
