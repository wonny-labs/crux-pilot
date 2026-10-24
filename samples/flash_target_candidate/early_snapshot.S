/* SPDX-License-Identifier: Apache-2.0 */
/* Pre-C capture only. NOR remains available; this is NOT the Flash busy island. */
.syntax unified
.thumb
.section .text.__wrap_soc_reset_hook,"ax",%progbits
.global __wrap_soc_reset_hook
.type __wrap_soc_reset_hook,%function
.thumb_func
__wrap_soc_reset_hook:
    mrs r12, PRIMASK
    cpsid i
    /* Refuse unknown MPU/cache ownership before relying on any RAM store. */
    ldr r1, =0xe000ed94
    ldr r2, [r1]
    tst r2, #1
    bne pilot_entry_refused
    ldr r1, =0xe000ed14
    ldr r3, [r1]
    tst r3, #0x10000
    bne pilot_entry_refused
    ldr r0, =pilot_reset_snapshot
    movs r1, #0
    str r1, [r0, #0]
    str r2, [r0, #12]
    str r3, [r0, #8]
    ldr r1, =0x400b8000
    ldrh r2, [r1, #0]
    str r2, [r0, #16]
    ldrh r2, [r1, #8]
    str r2, [r0, #20]
    ldr r1, =0x400d0000
    ldrh r2, [r1, #0]
    str r2, [r0, #24]
    ldrh r2, [r1, #8]
    str r2, [r0, #28]
    ldr r1, =0x400bc000
    ldr r2, [r1, #0]
    str r2, [r0, #32]
    ldr r1, =0x400f8000
    ldr r2, [r1, #8]
    str r2, [r0, #36]
    ldr r1, =0x400ac000
    ldr r2, [r1, #64]
    str r2, [r0, #40]
    ldr r2, [r1, #68]
    str r2, [r0, #44]
    movs r1, #1
    str r1, [r0, #4]
    movs r1, #0
    str r1, [r0, #48]
    str r12, [r0, #52]
    str r1, [r0, #56]
    str r1, [r0, #60]
    /* Preserve full raw state; refuse before SystemInit can change it. */
    ldr r1, [r0, #16]
    ldr r2, [r0, #24]
    orrs r1, r2
    tst r1, #4
    bne pilot_armed_refused
    ldr r1, [r0, #20]
    ldr r2, [r0, #28]
    orrs r1, r2
    tst r1, #1
    bne pilot_armed_refused
    ldr r1, [r0, #32]
    tst r1, #0x80
    bne pilot_armed_refused
    dsb
    ldr r1, =0x50494c54
    str r1, [r0, #0]
    dsb
    msr PRIMASK, r12
    b __real_soc_reset_hook
pilot_armed_refused:
    movs r1, #1
    str r1, [r0, #48]
    dsb
    /* Marker stays invalid. No watchdog feed, disable, reset, or NOR operation. */
pilot_entry_refused:
    b pilot_entry_refused
.ltorg
.size __wrap_soc_reset_hook, .-__wrap_soc_reset_hook

.section .pilot_snapshot,"aw",%nobits
.balign 64
.global pilot_reset_snapshot
.global pilot_reset_snapshot_end
pilot_reset_snapshot:
.space 64
pilot_reset_snapshot_end:
