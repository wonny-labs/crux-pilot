#include <assert.h>
#include <string.h>
volatile struct crux_observation_record crux_observation_result;
bool crux_rom_read_capture_supported(struct crux_rom_profile *profile) {
    profile->mcr0 = 0x10;
    return profile_ok;
}
int main(void) {
    crux_observation_result.pre_c[2] = 99;
    assert(crux_observation_capture_runtime() == -EACCES);
    assert(!crux_observation_result.timer_available && !timer_accesses);
    ccm.CCGR0 = CCM_CCGR0_CG12(3) | CCM_CCGR0_CG13(3);
    gpt.CNT = 42;
    assert(crux_observation_capture_runtime() == 0);
    assert(crux_observation_result.pre_c[2] == 99);
    assert(crux_observation_result.runtime_ccr == 0);
    assert(crux_observation_result.runtime_profile.mcr0 == 0x10);
    assert(crux_observation_result.timer_available == 1);
    assert(crux_observation_result.gpt2_before[4] == 42);
    assert(crux_observation_result.nominal_osc_hz == 24000000);
    for (unsigned fault = 0; fault < 10; ++fault) {
        scb.CCR = mpu.CTRL = wdog1.WCR = wdog1.WMCR = rtwdog.CS = 0;
        scb.CPACR = fpu.FPCCR = control = ipsr = 0;
        profile_ok = true;
        switch (fault) {
        case 0:
            scb.CCR = 0x10000;
            break;
        case 1:
            mpu.CTRL = 1;
            break;
        case 2:
            wdog1.WCR = 4;
            break;
        case 3:
            wdog1.WMCR = 1;
            break;
        case 4:
            rtwdog.CS = 0x80;
            break;
        case 5:
            scb.CPACR = 0xf00000;
            break;
        case 6:
            fpu.FPCCR = 1;
            break;
        case 7:
            control = 1;
            break;
        case 8:
            ipsr = 1;
            break;
        case 9:
            profile_ok = false;
            break;
        }
        assert(crux_observation_capture_runtime() == -EACCES);
        assert(crux_observation_result.phase != CRUX_OBSERVATION_COMPLETE);
    }
    gpt.CNT = 65;
    crux_observation_capture_timer_after();
    assert(crux_observation_result.gpt2_after[4] == 65);
    assert(gpt.CNT == 65); /* no peripheral mutation */
    return 0;
}
