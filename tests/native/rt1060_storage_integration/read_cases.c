#include <assert.h>
int main(int argc, char **argv) {
    assert(argc == 2);
    int test = atoi(argv[1]);
    uint8_t bytes[4] = {0};
    struct crux_rom_request r = {CRUX_ROM_READ, CRUX_PILOT_FLASH_FILE, 0x620000, 4, NULL};
    assert(crux_rom_command_read_result(bytes, sizeof(bytes)) == -EACCES);
    assert(crux_rom_command_prepare(&r) == 0);
    assert(crux_rom_command_read_result(bytes, sizeof(bytes)) == -EACCES);
    assert(crux_rom_command_dispatch() == 0);
    assert(crux_rom_command_read_result(bytes, 3) == -EINVAL);
    assert(crux_rom_command_read_result(bytes, 4) == 0);
    assert(bytes[0] == 0xa5 && bytes[3] == 0xa5);
    assert(crux_rom_command_read_result(bytes, 4) == -EACCES);
    r.operation = CRUX_ROM_PROGRAM;
    r.payload = bytes;
    assert(crux_rom_command_prepare(&r) == 0);
    assert(crux_rom_command_dispatch() == 0);
    assert(crux_rom_command_read_result(bytes, 4) == -EACCES);
    r.operation = CRUX_ROM_READ;
    assert(crux_rom_command_prepare(&r) == 0);
    if (test != 0) {
        transfers = polls = ticks = idle_calls = 0;
        failure = test == 1 ? 6 : test == 2 ? 5 : test == 3 ? 7 : 10;
        if (!setjmp(terminal)) {
            (void)crux_rom_command_dispatch();
            assert(0);
        }
        assert(crux_rom_command_read_result(bytes, 4) == -EACCES);
        return 0;
    }
    assert(crux_rom_command_dispatch() == 0);
    assert(crux_rom_command_prepare(NULL) == -EACCES);
    assert(crux_rom_command_read_result(bytes, 4) == -EACCES);
    return 0;
}
