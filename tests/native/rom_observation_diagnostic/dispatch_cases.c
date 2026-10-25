static void fresh(void) {
    memset(&regs, 0, sizeof(regs));
    memset(&crux_rom_slot, 0, sizeof(crux_rom_slot));
    memset(&crux_observation_nor, 0, sizeof(crux_observation_nor));
    memset((void *)&crux_observation_result, 0, sizeof(crux_observation_result));
    crux_observation_prepared = crux_observation_prepare_claimed = false;
    memcpy(regs.LUT, crux_rom_expected_lut, sizeof(regs.LUT));
    regs.LUTCR = 1;
    regs.MCR0 = 0x10;
    regs.STS0 = 3;
    regs.FLSHCR0[0] = 8192;
    regs.FLSHCR1[0] = 0x63;
    transfers = statuses = identities = finishes = reason = fail = wait_checks = 0;
}
static void healthy(void) {
    assert(crux_observation_prepare() == 0);
    assert(crux_rom_command_dispatch() == 0);
    assert(crux_observation_result.phase == CRUX_OBSERVATION_RESTORED);
    assert((crux_observation_result.flags &
            (CRUX_OBSERVATION_SLOT_INSTALLED | CRUX_OBSERVATION_SLOT_RESTORED |
             CRUX_OBSERVATION_KNOWN_READY)) ==
           (CRUX_OBSERVATION_SLOT_INSTALLED | CRUX_OBSERVATION_SLOT_RESTORED |
            CRUX_OBSERVATION_KNOWN_READY));
    assert((crux_observation_result.flags &
            (CRUX_OBSERVATION_INITIAL_STATUS_VALID | CRUX_OBSERVATION_ID_VALID |
             CRUX_OBSERVATION_FINAL_STATUS_VALID)) ==
           (CRUX_OBSERVATION_INITIAL_STATUS_VALID | CRUX_OBSERVATION_ID_VALID |
            CRUX_OBSERVATION_FINAL_STATUS_VALID));
    assert(crux_observation_result.status_initial == 0 &&
           crux_observation_result.status_final == 0);
    assert(transfers == 3 && statuses == 2 && identities == 1 && finishes == 1);
    assert(crux_observation_result.id[0] == (id_value & 255));
    assert(crux_observation_result.id[1] == ((id_value >> 8) & 255));
    assert(crux_observation_result.id[2] == ((id_value >> 16) & 255));
    assert(!crux_rom_slot.installed && regs.LUTCR == 1);
    assert(!memcmp(regs.LUT, crux_rom_expected_lut, sizeof(regs.LUT)));
    assert(crux_observation_prepare() == -EALREADY);
}
int main(int argc, char **argv) {
    assert(argc == 2);
    int test = atoi(argv[1]);
    fresh();
    if (test == 0)
        healthy();
    if (test == 1) {
        uint32_t identities_to_report[] = {0, 0xffffff, 0x16609d, 0x1770ef};
        for (unsigned i = 0; i < 4; ++i) {
            fresh();
            id_value = identities_to_report[i];
            healthy();
            assert(!(crux_observation_result.flags & CRUX_OBSERVATION_ID_MATCH));
        }
    }
    if (test == 2) {
        for (unsigned word = 0; word < 64; word++) {
            fresh();
            regs.LUT[word] ^= 1;
            assert(crux_observation_prepare() == 0);
            if (!setjmp(terminal)) {
                crux_rom_command_dispatch();
                assert(0);
            }
            assert(reason == -EACCES && transfers == 0 && !regs.LUTKEY);
        }
    }
    if (test == 3) {
        for (fail = 2; fail <= 12; fail++) {
            int failure = fail;
            assert(crux_observation_prepare() == 0);
            if (failure == 5) {
                assert(crux_rom_command_dispatch() == 0);
                assert(crux_observation_result.phase == CRUX_OBSERVATION_REFUSED);
                assert(statuses == 1 && !identities && !regs.LUTKEY && finishes == 1);
            } else {
                if (!setjmp(terminal)) {
                    crux_rom_command_dispatch();
                    assert(0);
                }
                assert(reason < 0 && crux_observation_result.phase == CRUX_OBSERVATION_CONTAINED);
                assert(crux_observation_result.phase != CRUX_OBSERVATION_COMPLETE);
                assert(finishes == 0);
                if (failure == 4)
                    assert(statuses == 1 && identities == 0 && !regs.LUTKEY);
                if (failure == 9)
                    assert(statuses == 2); /* no WIP polling */
            }
            fresh();
            fail = failure;
        }
    }
    if (test == 4) {
        if (!setjmp(terminal)) {
            crux_rom_command_dispatch();
            assert(0);
        }
        assert(reason == -EACCES && !transfers);
    }
    if (test == 5) {
        fail = 1;
        assert(crux_observation_prepare() == -EACCES);
        assert(!transfers);
        fail = 0;
        assert(crux_observation_prepare() == -EALREADY); /* latched claim */
    }
    if (test == 6) {
        regs.FLSHCR0[1] = 1;
        assert(crux_observation_prepare() == 0);
        if (!setjmp(terminal)) {
            crux_rom_command_dispatch();
            assert(0);
        }
        assert(reason == -EACCES && !transfers);
    }
    if (test == 7) {
        healthy();
        if (!setjmp(terminal)) {
            crux_rom_command_dispatch();
            assert(0);
        }
        assert(reason == -EACCES && transfers == 3); /* replay cannot issue */
    }
    return 0;
}
