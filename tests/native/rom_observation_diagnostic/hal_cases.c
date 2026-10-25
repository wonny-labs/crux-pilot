int main(void) {
    for (unsigned request = 0; request < 12; request++) {
        FLEXSPI_Type regs = {0};
        uint32_t bytes = 0;
        flexspi_transfer_t transfer = {
            .cmdType = kFLEXSPI_Read, .SeqNumber = 1, .dataSize = 1, .seqIndex = 1, .data = &bytes};
        switch (request) {
        case 0:
            transfer.cmdType = kFLEXSPI_Write;
            break;
        case 1:
            transfer.cmdType = kFLEXSPI_Config;
            break;
        case 2:
            transfer.port = 1;
            break;
        case 3:
            transfer.deviceAddress = 0x620000;
            break;
        case 4:
            transfer.SeqNumber = 2;
            break;
        case 5:
            transfer.data = NULL;
            break;
        case 6:
            transfer.seqIndex = 0;
            transfer.dataSize = 256;
            break; /* backup */
        case 7:
            transfer.seqIndex = 3;
            break; /* WREN */
        case 8:
            transfer.seqIndex = 5;
            break; /* erase */
        case 9:
            transfer.seqIndex = 9;
            break; /* program */
        case 10:
            transfer.seqIndex = 15;
            transfer.dataSize = 4;
            break;
        }
        waits = 0;
        if (!setjmp(terminal)) {
            FLEXSPI_TransferBlocking(&regs, request == 11 ? NULL : &transfer);
            assert(0);
        }
        assert(reason == -EACCES && waits == 0 && regs.IPCMD == 0 && idles == 0);
    }
    for (unsigned sequence = 0; sequence < 2; sequence++) {
        FLEXSPI_Type regs = {0};
        uint32_t bytes = 0;
        flexspi_transfer_t transfer = {.cmdType = kFLEXSPI_Read,
                                       .SeqNumber = 1,
                                       .dataSize = sequence ? 3 : 1,
                                       .seqIndex = sequence ? 15 : 1,
                                       .data = &bytes};
        mode = 6;
        regs.intr[1] = 4;
        assert(FLEXSPI_TransferBlocking(&regs, &transfer) == 0);
        assert(regs.IPCMD == 1 && waits > 0);
    }
    FLEXSPI_Type regs = {0};
    uint32_t bytes = 0;
    flexspi_transfer_t transfer = {
        .cmdType = kFLEXSPI_Read, .SeqNumber = 1, .dataSize = 1, .seqIndex = 1, .data = &bytes};
    changing = &transfer;
    mode = 6;
    regs.intr[1] = 4;
    assert(FLEXSPI_TransferBlocking(&regs, &transfer) == 0);
    assert(transfer.cmdType == kFLEXSPI_Write); /* lower guard must use its owned snapshot */
    return 0;
}
