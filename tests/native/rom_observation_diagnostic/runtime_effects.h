#ifndef OBSERVATION_RUNTIME_EFFECTS_H
#define OBSERVATION_RUNTIME_EFFECTS_H
#include <stdbool.h>
#include <stdint.h>
static struct {
    uint32_t CCR, CPACR;
} scb;
static struct {
    uint32_t CTRL;
} mpu;
static struct {
    uint32_t FPCCR;
} fpu;
static struct {
    uint32_t WCR, WMCR;
} wdog1, wdog2;
static struct {
    uint32_t CS;
} rtwdog;
static struct {
    uint32_t CCGR0;
} ccm;
static struct {
    uint32_t LOWPWR_CTRL, MISC0;
} xtal;
static struct {
    uint32_t CR, PR, IR, SR, CNT;
} gpt;
static unsigned timer_accesses;
static void *gpt_access(void) {
    timer_accesses++;
    return &gpt;
}
#define SCB (&scb)
#define MPU (&mpu)
#define FPU (&fpu)
#define WDOG1 (&wdog1)
#define WDOG2 (&wdog2)
#define RTWDOG (&rtwdog)
#define CCM (&ccm)
#define XTALOSC24M (&xtal)
#define GPT2 ((typeof_gpt *)gpt_access())
typedef struct {
    uint32_t CR, PR, IR, SR, CNT;
} typeof_gpt;
#define CCM_CCGR0_CG12(x) ((x) << 24)
#define CCM_CCGR0_CG13(x) ((x) << 26)
static uint32_t control, ipsr, primask;
static uint32_t __get_CONTROL(void) {
    return control;
}
static uint32_t __get_IPSR(void) {
    return ipsr;
}
static uint32_t __get_PRIMASK(void) {
    return primask;
}
static uint32_t CLOCK_GetOscFreq(void) {
    return 24000000;
}
static bool profile_ok = true;
#endif
