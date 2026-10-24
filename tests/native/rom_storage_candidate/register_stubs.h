/* Test-only complete set of registers consumed by read-confirmation. */
#include <stdint.h>
static struct {
    uint32_t CSCMR1, CCSR, CCGR6;
} ccm;
static struct {
    uint32_t PLL_USB1, PLL_SYS, PFD_480, PFD_528, PLL_SYS_NUM, PLL_SYS_DENOM;
} analog;
static struct {
    uint32_t MCR0, MCR2, STS0, FLSHCR0[4], FLSHCR1[4], FLSHCR2[4], LUT[64];
} flexspi;
#define CCM (&ccm)
#define CCM_ANALOG (&analog)
#define FLEXSPI (&flexspi)
#define kCLOCK_FlexspiClkRoot 2
static unsigned getters, change_clock;
static uint32_t rate = 100000000U, osc = 24000000U;
static uint32_t CLOCK_GetOscFreq(void) {
    return osc;
}
static uint32_t CLOCK_GetClockRootFreq(unsigned root) {
    (void)root;
    ++getters;
    if (change_clock) {
        ccm.CSCMR1 ^= 1;
    }
    return rate;
}
