/* SPDX-License-Identifier: Apache-2.0 */
/* Read-only refusal before the ordinary early clock hook. NOR is unchanged. */
.syntax unified
.thumb
.section .text.__wrap_soc_early_init_hook,"ax",%progbits
.global __wrap_soc_early_init_hook
.type __wrap_soc_early_init_hook,%function
.thumb_func
__wrap_soc_early_init_hook:
    /* Mux0 can inherit SEMC/PERIPH, which the selected hook changes. */
    ldr r0, =0x400fc01c
    ldr r0, [r0]
    tst r0, #0x60000000
    beq crux_clock_entry_refused
    /* Mux1/2/3 sources are untouched by the exact selected clock_init. */
    b __real_soc_early_init_hook
.global crux_clock_entry_refused
crux_clock_entry_refused:
    cpsid i
.Lclock_refused:
    b .Lclock_refused
.ltorg
.size __wrap_soc_early_init_hook, .-__wrap_soc_early_init_hook
