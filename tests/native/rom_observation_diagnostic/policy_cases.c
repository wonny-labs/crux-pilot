#include <assert.h>
#include <stdlib.h>
#include <string.h>
static unsigned captures, preflights, preparations, timer_inits, begins, enters;
static int fault;
const volatile uint32_t pilot_reset_snapshot[16] = {0x50494c54, 1, 0, 0, 0, 0, 0, 0, 0, 19};
int crux_observation_capture_runtime(void) {
    captures++;
    crux_observation_result.runtime_ccr = 0x1234;
    return fault == 1 ? -EACCES : 0;
}
void crux_observation_capture_timer_after(void) {
    assert(crux_observation_result.phase == CRUX_OBSERVATION_RESTORED);
    crux_observation_result.gpt2_after[4] = 77;
}
int crux_flash_island_prepare(void) {
    preflights++;
    return fault == 2 ? -EACCES : 0;
}
int crux_observation_prepare(void) {
    preparations++;
    return fault == 3 ? -EACCES : 0;
}
int crux_rom_gpt2_candidate_init(void) {
    timer_inits++;
    return fault == 4 ? -ETIMEDOUT : 0;
}
int crux_rom_candidate_deadline_begin(uint64_t ticks) {
    assert(ticks == 2400000);
    begins++;
    return fault == 5 ? -EACCES : 0;
}
int crux_island_enter(void) {
    enters++;
    assert(crux_observation_result.phase == CRUX_OBSERVATION_ENTERING);
    assert(crux_observation_result.phase != CRUX_OBSERVATION_COMPLETE);
    if (fault == 6)
        return -EACCES; /* context refused before switching */
    if (fault == 7) {   /* positively ready, unsupported WEL; context restored */
        crux_observation_result.result = -EACCES;
        crux_observation_result.phase = CRUX_OBSERVATION_REFUSED;
        crux_observation_result.disposition = CRUX_OBSERVATION_RETURNED;
        return 0;
    }
    crux_observation_result.phase = CRUX_OBSERVATION_RESTORED;
    crux_observation_result.status_initial = 0;
    crux_observation_result.status_final = 0;
    crux_observation_result.id[0] = 0x9d;
    return 0;
}
int main(int argc, char **argv) {
    assert(argc == 2);
    int test = atoi(argv[1]);
    fault = test;
    assert(crux_observation_result.magic == 0x4f425356);
    assert(crux_observation_result.version == 1);
    assert(crux_observation_result.size == sizeof(crux_observation_result));
    assert(crux_observation_result.phase == CRUX_OBSERVATION_BOOT);
    int rc = crux_observation_run(test == 8 ? 0x42 : CRUX_OBSERVATION_ID_STATUS);
    if (test == 0 || test == 9) {
        assert(rc == 0);
        assert(crux_observation_result.phase == CRUX_OBSERVATION_COMPLETE);
        assert(crux_observation_result.disposition == CRUX_OBSERVATION_RETURNED);
        assert(crux_observation_result.gpt2_after[4] == 77);
        assert(crux_observation_result.pre_c[9] == 19);
        assert(crux_observation_result.runtime_ccr == 0x1234);
        assert(captures == 1 && preflights == 1 && preparations == 1);
        assert(timer_inits == 1 && begins == 1 && enters == 1);
    } else {
        assert(rc < 0);
        assert(crux_observation_result.phase == CRUX_OBSERVATION_REFUSED);
        if (test == 8 || test == 10)
            assert(!captures && !preflights && !preparations && !timer_inits && !begins && !enters);
        if (test == 1)
            assert(!preflights && !preparations && !timer_inits && !begins && !enters);
        if (test == 2)
            assert(!preparations && !timer_inits && !begins && !enters);
        if (test == 3)
            assert(!timer_inits && !begins && !enters);
        if (test == 4)
            assert(!begins && !enters);
        if (test == 5)
            assert(!enters);
    }
    struct crux_observation_record before;
    memcpy(&before, (const void *)&crux_observation_result, sizeof(before));
    unsigned prior = enters;
    assert(crux_observation_run(CRUX_OBSERVATION_ID_STATUS) == -EALREADY);
    assert(enters == prior);
    assert(!memcmp(&before, (const void *)&crux_observation_result, sizeof(before)));
    return 0;
}
