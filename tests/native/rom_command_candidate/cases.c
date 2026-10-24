int main(int argc, char **argv) {
    if (argc != 4)
        return 2;
    int op = atoi(argv[1]), invalid = atoi(argv[2]);
    failure = atoi(argv[3]);
    uint8_t payload[4] = {90, 91, 92, 93};
    struct crux_rom_request request = {op, CRUX_PILOT_FLASH_FILE, 0x620000,
                                       op == 2   ? 4096
                                       : op == 4 ? 1
                                                 : 4,
                                       payload};
    switch (invalid) {
    case 1:
        request.offset = -1;
        break;
    case 2:
        request.offset = 0;
        break;
    case 3:
        request.offset = 0x61ffff;
        break;
    case 4:
        request.offset = 0x7eefff;
        break;
    case 5:
        request.offset = 0x800000;
        break;
    case 6:
        request.length = 0;
        break;
    case 7:
        request.length = 257;
        break;
    case 8:
        request.offset = 0x6200ff;
        break;
    case 9:
        request.payload = NULL;
        break;
    case 10:
        request.owner = 99;
        break;
    case 11:
        request.operation = 2;
        request.length = 8192;
        break;
    case 12:
        request.operation = 2;
        request.offset++;
        request.length = 4096;
        break;
    case 13:
        request.operation = 99;
        break;
    }
    int ret = crux_rom_command_prepare(&request);
    payload[0] = 0;
    if (!ret) {
        ret = 777;
        if (!setjmp(terminal))
            ret = crux_rom_command_dispatch();
    }
    printf("%d %d %s %d\n", ret, reason, trace, programmed);
    return 0;
}
