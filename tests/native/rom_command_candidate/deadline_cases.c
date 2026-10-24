static void fresh(void) {
    if (!gpt_page) {
        gpt_page = aligned_alloc(4096, 4096);
        assert(gpt_page);
    }
    memset(&gpt, 0, sizeof(gpt));
    memset(&crux_rom_deadline, 0, sizeof(crux_rom_deadline));
    timer_mode = 0;
    counter_step = 1;
    reset_reads = 0;
    cr_reads = 0;
    nominal = 24000000;
    ccm.CCGR0 = CCM_CCGR0_CG12(3) | CCM_CCGR0_CG13(3);
    xtal.LOWPWR_CTRL = XTALOSC24M_LOWPWR_CTRL_XTALOSC_PWRUP_STAT_MASK;
    xtal.MISC0 = XTALOSC24M_MISC0_OSC_XTALOK_MASK;
}
static void initialized(void) {
    fresh();
    assert(crux_rom_gpt2_candidate_init() == 0);
}
int main(int argc, char **argv) {
    assert(argc == 2);
    int test = atoi(argv[1]);
    if (test == 0) {
        for (unsigned i = 0; i < 11; i++) {
            fresh();
            switch (i) {
            case 0:
                gpt.cr[0] = GPT_CR_EN_MASK;
                break;
            case 1:
                gpt.IR = 1;
                break;
            case 2:
                gpt.PR = 1;
                break;
            case 3:
                gpt.SR = 1;
                break;
            case 4:
                gpt.CNT = 1;
                break;
            case 5:
                ccm.CCGR0 = 0;
                break;
            case 6:
                ccm.CCGR0 ^= CCM_CCGR0_CG13(2);
                break;
            case 7:
                xtal.LOWPWR_CTRL |= XTALOSC24M_LOWPWR_CTRL_OSC_SEL_MASK;
                break;
            case 8:
                xtal.MISC0 = 0;
                break;
            case 9:
                nominal = 25000000;
                break;
            case 10:
                timer_mode = 6;
                break;
            }
            GPT_Type before;
            memcpy(&before, &gpt, sizeof(gpt));
            uint32_t gates = ccm.CCGR0, osc = xtal.LOWPWR_CTRL;
            assert(mprotect(gpt_page, 4096, PROT_READ) == 0);
            assert(crux_rom_gpt2_candidate_init() == -EACCES);
            assert(mprotect(gpt_page, 4096, PROT_READ | PROT_WRITE) == 0);
            assert(!memcmp(&before, &gpt, sizeof(gpt)));
            assert(gates == ccm.CCGR0);
            if (i != 10)
                assert(osc == xtal.LOWPWR_CTRL);
            assert(reset_reads == 0);
            assert(crux_rom_candidate_deadline_expired());
        }
    } else if (test == 1) {
        initialized();
        assert(reset_reads == 1);
        assert(gpt.PR == 0 && gpt.IR == 0);
        assert(gpt.cr[0] == (GPT_CR_FRR_MASK | GPT_CR_ENMOD_MASK | GPT_CR_CLKSRC(5) |
                             GPT_CR_EN_24M_MASK | GPT_CR_EN_MASK));
        assert(crux_rom_gpt2_candidate_init() == -EACCES);
        for (unsigned fault = 1; fault <= 3; fault++) {
            fresh();
            timer_mode = fault;
            assert(crux_rom_gpt2_candidate_init() != 0);
            assert(reset_reads <= 1025);
            assert(crux_rom_candidate_deadline_expired());
        }
    } else if (test == 2) {
        uint64_t bad[] = {0, 0x80000000ULL, 0x100000001ULL, UINT64_MAX};
        for (unsigned i = 0; i < 4; i++) {
            initialized();
            assert(crux_rom_candidate_deadline_begin(bad[i]) == -EINVAL);
            assert(crux_rom_candidate_deadline_expired());
        }
        initialized();
        assert(crux_rom_candidate_deadline_begin(4) == 0);
        for (unsigned i = 0; i < 3; i++)
            assert(!crux_rom_candidate_deadline_expired());
        assert(crux_rom_candidate_deadline_expired());
        assert(crux_rom_candidate_deadline_expired());
        initialized();
        gpt.CNT = UINT32_MAX - 2;
        assert(crux_rom_candidate_deadline_begin(4) == 0);
        for (unsigned i = 0; i < 3; i++) {
            assert(!crux_rom_candidate_deadline_expired());
        }
        assert(gpt.CNT < 4);
        assert(crux_rom_candidate_deadline_expired());
        initialized();
        assert(crux_rom_candidate_deadline_begin(10000) == 0);
        timer_mode = 3;
        unsigned n = 0;
        while (!crux_rom_candidate_deadline_expired())
            assert(++n <= 1024);
        assert(n > 0);
        initialized();
        assert(crux_rom_candidate_deadline_begin(100) == 0);
        gpt.CNT -= 2;
        timer_mode = 3;
        assert(crux_rom_candidate_deadline_expired());
    } else if (test == 4) {
        initialized();
        for (unsigned operation = 0; operation < 4; ++operation) {
            assert(crux_rom_candidate_deadline_begin(8) == 0);
            assert(!crux_rom_candidate_deadline_expired());
            assert(!crux_rom_candidate_deadline_expired());
            crux_rom_candidate_deadline_finish();
        }
        assert(crux_rom_candidate_deadline_begin(8) == 0);
        while (!crux_rom_candidate_deadline_expired()) {
        }
        assert(crux_rom_candidate_deadline_begin(8) == -EACCES);
        assert(crux_rom_gpt2_candidate_init() == -EACCES);
        assert(crux_rom_candidate_deadline_expired());
    } else if (test == 5) {
        fresh();
        nominal = 25000000;
        assert(crux_rom_gpt2_candidate_init() == -EACCES);
        nominal = 24000000;
        unsigned before = reset_reads;
        assert(crux_rom_gpt2_candidate_init() == -EACCES);
        assert(reset_reads == before);
        initialized();
        assert(crux_rom_candidate_deadline_begin(8) == 0);
        assert(crux_rom_candidate_deadline_begin(8) == -EACCES);
        assert(crux_rom_candidate_deadline_expired());
        initialized();
        assert(crux_rom_candidate_deadline_begin(8) == 0);
        gpt.CNT += 100;
        assert(crux_rom_candidate_deadline_begin(8) == -EACCES);
        assert(crux_rom_candidate_deadline_expired());
        initialized();
        assert(crux_rom_candidate_deadline_begin(8) == 0);
        gpt.CNT += 100;
        terminal_reason = 0;
        if (!setjmp(terminal)) {
            crux_rom_candidate_deadline_finish();
            assert(0);
        }
        assert(terminal_reason == -ETIMEDOUT);
    } else {
        for (unsigned i = 0; i < 7; i++) {
            initialized();
            assert(crux_rom_candidate_deadline_begin(100) == 0);
            switch (i) {
            case 0:
                gpt.cr[0] ^= GPT_CR_EN_MASK;
                break;
            case 1:
                gpt.PR = 1;
                break;
            case 2:
                gpt.IR = 1;
                break;
            case 3:
                ccm.CCGR0 = 0;
                break;
            case 4:
                xtal.LOWPWR_CTRL ^= 1;
                break;
            case 5:
                xtal.MISC0 = 0;
                break;
            case 6:
                gpt.cr[0] ^= GPT_CR_CLKSRC(1);
                break;
            }
            assert(crux_rom_candidate_deadline_expired());
            assert(crux_rom_candidate_deadline_begin(100) != 0);
            terminal_reason = 0;
            if (!setjmp(terminal)) {
                crux_rom_candidate_wait_check();
                assert(0);
            }
            assert(terminal_reason == -ETIMEDOUT);
        }
    }
    return 0;
}
