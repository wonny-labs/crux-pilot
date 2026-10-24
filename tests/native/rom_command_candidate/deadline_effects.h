#include <assert.h>
#include <errno.h>
#include <setjmp.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
enum { kGPT_ClockSource_Osc = 5 };
/* Controlled MMIO effects only. All timer policy lives in actual candidate C. */
static unsigned reset_reads, cr_reads, timer_mode;
static uint32_t counter_step = 1;
typedef struct {
    volatile uint32_t cr[1], PR, IR, SR, CNT;
} GPT_Type;
static GPT_Type *gpt_page;
#define gpt (*gpt_page)
static unsigned cr_index(void) {
    cr_reads++;
    if (gpt.cr[0] & GPT_CR_SWR_MASK) {
        reset_reads++;
        if (timer_mode != 1) {
            gpt.cr[0] = 0;
            gpt.PR = 0;
            gpt.IR = 0;
            gpt.SR = 0;
            gpt.CNT = 0;
        }
    }
    return 0;
}
#define CR cr[cr_index()]
#define GPT2 (&gpt)
static struct {
    volatile uint32_t CCGR0;
} ccm;
#define CCM (&ccm)
static struct {
    volatile uint32_t LOWPWR_CTRL, MISC0;
} xtal;
#define XTALOSC24M (&xtal)
static uint32_t nominal = 24000000;
static uint32_t CLOCK_GetOscFreq(void) {
    return nominal;
}
static void GPT_StartTimer(GPT_Type *b) {
    b->CR |= GPT_CR_EN_MASK;
    if (timer_mode == 2)
        b->cr[0] ^= GPT_CR_FRR_MASK;
}
static uint32_t GPT_GetCurrentTimerCount(GPT_Type *b) {
    if (timer_mode != 3 && (b->cr[0] & GPT_CR_EN_MASK))
        b->CNT += counter_step;
    if (timer_mode == 6 && !(b->cr[0] & GPT_CR_EN_MASK)) {
        xtal.LOWPWR_CTRL |= XTALOSC24M_LOWPWR_CTRL_OSC_SEL_MASK;
    }
    return b->CNT;
}
static jmp_buf terminal;
static int terminal_reason;
_Noreturn void crux_flash_poc_terminal(int reason) {
    terminal_reason = reason;
    longjmp(terminal, 1);
}
