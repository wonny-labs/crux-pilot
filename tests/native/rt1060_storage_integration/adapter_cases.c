#include "storage_adapter.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>
static int prereq = -EACCES, fail_at, calls;
static size_t lengths[8];
static int64_t offsets[8];
int crux_candidate_command_prerequisites(void) {
    return prereq;
}
int crux_rom_command_submit(const struct crux_rom_request *r) {
    assert(r->owner == CRUX_PILOT_FLASH_FILE);
    lengths[calls] = r->length;
    offsets[calls++] = r->offset;
    return calls == fail_at ? -EIO : 0;
}
int crux_rom_command_read_result(void *bytes, size_t length) {
    memset(bytes, 0xa5, length);
    return 0;
}
int main(int argc, char **argv) {
    assert(argc == 2);
    int test = atoi(argv[1]);
    uint8_t data[600] = {0};
    assert(crux_candidate_read(CRUX_PILOT_FLASH_FILE, 0x620000, data, 1) == -EACCES);
    if (test != 0)
        prereq = 0;
    if (test != 0) {
        assert(crux_candidate_storage_begin() == 0);
        assert(crux_candidate_storage_begin() == -EACCES);
        if (test == 1) {
            assert(crux_candidate_write(CRUX_PILOT_FLASH_FILE, 0x6200ff, data, 600) == 0);
            assert(calls == 4 && lengths[0] == 1 && lengths[1] == 256 && lengths[2] == 256 &&
                   lengths[3] == 87);
            assert(offsets[1] == 0x620100 && offsets[3] == 0x620300);
        } else if (test == 2) {
            assert(crux_candidate_read(CRUX_PILOT_FLASH_FILE, 0x620000, data, 600) == 0);
            assert(calls == 3 && lengths[0] == 256 && lengths[2] == 88);
            assert(data[0] == 0xa5 && data[599] == 0xa5);
        } else if (test == 3) {
            assert(crux_candidate_write(CRUX_PILOT_FLASH_NVS, 0x620000, data, 1) == -EACCES);
            assert(crux_candidate_write(CRUX_PILOT_FLASH_FILE, 0x7eefff, data, 2) == -EACCES);
            assert(crux_candidate_erase(CRUX_PILOT_FLASH_FILE, 0x620001, 4096) == -EACCES);
            assert(crux_candidate_erase(CRUX_PILOT_FLASH_FILE, 0x620000, 8192) == 0);
            assert(calls == 2 && lengths[0] == 4096 && offsets[1] == 0x621000);
        } else {
            fail_at = 2;
            if (test == 4) {
                assert(crux_candidate_read(CRUX_PILOT_FLASH_FILE, 0x620000, data, 600) == -EIO);
                assert(calls == 2 && data[255] == 0xa5 && data[256] == 0);
            } else if (test == 5) {
                assert(crux_candidate_write(CRUX_PILOT_FLASH_FILE, 0x6200ff, data, 600) == -EIO);
                assert(calls == 2 && lengths[0] == 1 && offsets[1] == 0x620100);
            } else {
                assert(crux_candidate_erase(CRUX_PILOT_FLASH_FILE, 0x620000, 12288) == -EIO);
                assert(calls == 2 && lengths[0] == 4096 && offsets[1] == 0x621000);
            }
            assert(crux_candidate_write(CRUX_PILOT_FLASH_FILE, 0x620000, data, 1) == -EACCES);
            assert(calls == 2);
            assert(!crux_candidate_storage_finish());
            return 0;
        }
        assert(crux_candidate_storage_finish());
        assert(crux_candidate_read(CRUX_PILOT_FLASH_FILE, 0x620000, data, 1) == -EACCES);
        assert(crux_candidate_storage_begin() == -EACCES);
        return 0;
    }
    assert(crux_candidate_storage_begin() == -EACCES);
    assert(calls == 0);
    prereq = 0;
    /* A refused begin is sticky: no in-process qualification retry. */
    assert(crux_candidate_storage_begin() == -EACCES);
    return 0;
}
