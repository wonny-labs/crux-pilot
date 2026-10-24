/* SPDX-License-Identifier: Apache-2.0 */
static void fresh(void) {
    memset(&regs, 0, sizeof(regs));
    memset(&crux_rom_slot, 0, sizeof(crux_rom_slot));
    memcpy(regs.LUT, crux_rom_expected_lut, sizeof(regs.LUT));
    regs.LUTCR = 1;
    regs.STS0 = 3;
    regs.MCR0 = 0x10;
    regs.FLSHCR0[0] = 8192;
    regs.FLSHCR1[0] = 0x63;
    reason = commands = writes = id_reads = statuses = updates = checks = idle_calls = fail = 0;
    id_value = 0x17709d;
    wel = false;
    busy_polls = 0;
    readback_checks = 0;
    memset(programmed, 0, 4);
    uint8_t payload[] = {90, 91, 92, 93};
    struct crux_rom_request r = {CRUX_ROM_PROGRAM, CRUX_PILOT_FLASH_FILE, 0x620000, 4, payload};
    assert(crux_rom_command_prepare(&r) == 0);
    memset(payload, 0, 4);
}
static int dispatch(void) {
    if (setjmp(terminal))
        return 777;
    return crux_rom_command_dispatch();
}
static void refused(void) {
    assert(dispatch() == 777);
    assert(reason);
    assert(!commands && !writes && !regs.LUTKEY);
}
int main(int argc, char **argv) {
    assert(argc == 2);
    int test = atoi(argv[1]);
    if (test == 0 || test == 6) {
        fresh();
        busy_polls = 2;
        if (test == 6)
            regs.IPCR1 = 0x070f0000;
        assert(dispatch() == 0);
        assert(id_reads == 1 && writes == 2 && statuses >= 5);
        assert(!memcmp(programmed, (uint8_t[]){90, 91, 92, 93}, 4));
        assert(!memcmp(regs.LUT, crux_rom_expected_lut, sizeof(regs.LUT)));
        assert(regs.LUTCR == 1 && !crux_rom_slot.installed);
        return 0;
    }
    if (test == 1)
        for (unsigned word = 0; word < 64; word++) {
            fresh();
            regs.LUT[word] ^= 1;
            refused();
        }
    if (test == 2) {
        for (unsigned port = 0; port < 4; port++)
            for (unsigned bit = 0; bit < 16; bit++) {
                if (!(0xefefU & (1U << bit)))
                    continue;
                fresh();
                regs.FLSHCR2[port] = 1U << bit;
                refused();
            }
        for (unsigned state = 0; state < 4; state++)
            if (state != 1) {
                fresh();
                regs.LUTCR = state;
                refused();
            }
        for (unsigned port = 1; port < 4; port++) {
            fresh();
            regs.FLSHCR0[port] = 1;
            refused();
        }
        for (unsigned state = 0; state < 3; state++) {
            fresh();
            regs.STS0 = state;
            regs.IPCR1 = 0x070f0000;
            refused();
        }
        fresh();
        regs.MCR0 = 0;
        refused();
        fresh();
        regs.MCR2 = 0x8000;
        refused();
        fresh();
        regs.FLSHCR1[0] ^= 1;
        refused();
        fresh();
        regs.FLSHCR0[0] = 8191;
        refused();
    }
    if (test == 3) {
        unsigned ids[] = {0, 0xffffff, 0x17709c, 0x17719d, 0x18709d};
        for (unsigned i = 0; i < sizeof(ids) / sizeof(ids[0]); i++) {
            fresh();
            id_value = ids[i];
            assert(dispatch() == 777);
            assert(reason == -ENODEV && !writes);
            assert(!memcmp(regs.LUT, crux_rom_expected_lut, sizeof(regs.LUT)) && regs.LUTCR == 1);
        }
    }
    if (test == 4) {
        /* Every actual transfer stage errors terminally: initial status, ID,
         * post-ID ready, WREN, WEL status, program, post-program status. */
        for (int error = 1; error <= 7; error++) {
            fresh();
            fail = error;
            assert(dispatch() == 777 && reason == -EIO);
            if (error <= 3)
                assert(!writes);
        }
        for (int error = 100; error <= 120; error++) {
            if (error == 116 || error == 117)
                continue;
            fresh();
            fail = error;
            assert(dispatch() == 777);
            assert(reason);
            if (error < 120)
                assert(!writes);
            if (error == 103 || error == 106 || error == 109)
                assert(regs.LUT[error == 103   ? 10
                                : error == 106 ? 2
                                               : 59] != crux_rom_expected_lut[error == 103   ? 10
                                                                              : error == 106 ? 2
                                                                                             : 59]);
        }
    }
    if (test == 4) {
        for (unsigned saved = 0; saved < 7; ++saved) {
            fresh();
            if (!setjmp(terminal)) {
                crux_rom_identity_install(&crux_rom_nor.controller);
                crux_rom_ready_idle();
                if (saved < 4) {
                    crux_rom_slot.saved[saved] = 1;
                } else {
                    crux_rom_slot.saved_lock = saved == 4 ? 0 : saved == 5 ? 2 : 3;
                }
                crux_rom_identity_restore(&crux_rom_nor.controller);
                assert(0);
            }
            assert(reason == -EACCES && !writes);
            assert(regs.LUT[60] == crux_rom_id_lut[0] && crux_rom_slot.installed);
        }
    }
    if (test == 5)
        for (int error = 116; error <= 117; error++) {
            fresh();
            fail = error;
            assert(dispatch() == 777 && reason == -EACCES);
            assert(writes == 1 && !programmed[0]);
        }
    return 0;
}
