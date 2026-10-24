/* SPDX-License-Identifier: Apache-2.0 */
/* Private RAM context; target command admission remains refused. */
.syntax unified
.thumb
.section .candidate_island_text,"ax",%progbits
.balign 4
.global crux_island_enter
.type crux_island_enter,%function
.thumb_func
crux_island_enter:
    /* No context switch from exception, unprivileged, or inherited FP state. */
    mrs r0, IPSR
    cmp r0, #0
    bne .Lrefuse
    mrs r0, CONTROL
    tst r0, #5
    bne .Lrefuse
    ldr r1, =0xe000ed88
    ldr r0, [r1]
    tst r0, #0x00f00000
    bne .Lrefuse
    ldr r1, =0xe000ef34
    ldr r0, [r1]
    tst r0, #1
    bne .Lrefuse
    /* Flash is unchanged and available: save caller registers on its stack. */
    push {r4-r11, lr}
    mov r4, sp
    mrs r5, MSP
    mrs r6, PSP
    mrs r7, CONTROL
    mrs r9, PRIMASK
    cpsid i
    ldr r0, =0xe000ed08
    ldr r8, [r0]
    /* MSP is reserved for exceptions; thread operation uses its own PSP. */
    ldr r1, =__island_exception_top
    msr MSP, r1
    ldr r1, =__island_operation_top
    msr PSP, r1
    movs r1, #2
    msr CONTROL, r1
    isb
    ldr r1, =__island_vectors_start
    str r1, [r0]
    dsb
    isb
    /* This read confirms only current controller idle, never NOR WIP. */
    ldr r1, =0x402a8000
    ldr r0, [r1, #0]
    tst r0, #3             /* SWRESET or MDIS means uncertain. */
    bne crux_island_exception_terminal
    ldr r0, [r1, #0xe0]    /* STS0: SEQIDLE + ARBIDLE. */
    and r0, r0, #3
    cmp r0, #3
    bne crux_island_exception_terminal
    /* Only this typed private dispatcher runs after vectors/stacks/IRQ guard.
     * NONE preserves the original unchanged read-only observation. */
    bl crux_rom_command_dispatch
    cmp r0, #0
    bne crux_island_exception_terminal
    /* Preserve result across complete caller-context restoration. */
    mov r10, r0
    /* Return is reachable only from dispatcher positively completed/unchanged. */
    dsb
    ldr r0, =0xe000ed08
    str r8, [r0]
    dsb
    isb
    /* CONTROL may select MSP; restore MSP before switching back. */
    msr MSP, r5
    msr CONTROL, r7
    isb
    msr PSP, r6
    mov sp, r4
    mov r0, r9
    mov r12, r10
    pop {r4-r11, lr}
    /* Original register/stack context is complete before interrupts resume. */
    msr PRIMASK, r0
    mov r0, r12
    bx lr
.Lrefuse:
    movs r0, #13
    rsbs r0, r0, #0
    bx lr
.ltorg
.size crux_island_enter, .-crux_island_enter

.global crux_island_exception_terminal
.type crux_island_exception_terminal,%function
.thumb_func
crux_island_exception_terminal:
    cpsid i
.Lterminal:
    b .Lterminal
.size crux_island_exception_terminal, .-crux_island_exception_terminal

.section .candidate_island_vectors,"a",%progbits
.balign 1024
.word __island_exception_top
.rept 255
.word crux_island_exception_terminal
.endr
