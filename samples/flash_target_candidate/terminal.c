/* Experimental terminal only: no recovery or hardware safety guarantee. */
#include "flash_poc.h"

_Noreturn __attribute__((noinline)) void crux_flash_poc_terminal(int reason) {
    (void)reason;
    for (;;) {
        __asm__ volatile("nop");
    }
}
