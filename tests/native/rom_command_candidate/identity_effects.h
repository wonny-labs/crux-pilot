/* SPDX-License-Identifier: Apache-2.0 */
#include "rom_command.h"
#include <assert.h>
#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#define LOG_DBG(...) ((void)0)
#define JESD216_READ_ID_LEN 3
#define SPI_NOR_WIP_BIT 1U
#define SPI_NOR_WEL_BIT 2U
#define FLEXSPI_LUT_COUNT 64
#define FLEXSPI_LUT_KEY_VAL 0x5af05af0U
enum { kFLEXSPI_PortA1, kFLEXSPI_Read, kFLEXSPI_Write, kFLEXSPI_Command };
struct device {
    void *data;
    const void *config;
};
struct flash_flexspi_nor_data {
    struct device controller;
    unsigned port;
    bool legacy_poll;
};
typedef struct {
    unsigned deviceAddress, port, cmdType, SeqNumber, seqIndex;
    uint32_t *data;
    size_t dataSize;
} flexspi_transfer_t;
typedef struct {
    uint32_t LUT[64], LUTKEY, LUTCR, STS0, MCR0, MCR2, FLSHCR0[4], FLSHCR1[4], FLSHCR2[4], IPCR1;
} FLEXSPI_Type;
static FLEXSPI_Type regs;
static jmp_buf terminal;
static int reason, fail, commands, writes, id_reads, statuses, updates, checks, idle_calls;
static unsigned id_value, busy_polls, readback_checks;
static bool wel;
static uint8_t programmed[4];
static FLEXSPI_Type *get_base(const struct device *d) {
    (void)d;
    return &regs;
}
int crux_rom_memc_bind(struct device *d) {
    (void)d;
    return 0;
}
_Noreturn void crux_flash_poc_terminal(int e) {
    reason = e;
    longjmp(terminal, 1);
}
bool crux_rom_elapsed_expired(void) {
    return fail == 100 || checks > 10000;
}
void crux_rom_candidate_wait_check(void) {
    checks++;
    if (fail == 101 || checks > 10000)
        crux_flash_poc_terminal(-ETIMEDOUT);
    /* Readback-side register effects immediately after the actual SDK stores. */
    if (regs.LUTKEY && regs.LUTCR == 1 && ++readback_checks == 1) {
        if (fail == 102)
            regs.LUT[60] ^= 1;
        if (fail == 103)
            regs.LUT[10] ^= 1;
        if (fail == 104)
            regs.LUTCR = 2;
        if (fail == 105)
            regs.MCR2 ^= 1;
    }
    if (id_reads && fail == 106)
        regs.LUT[2] ^= 1;
    if (id_reads && fail == 107)
        regs.LUTCR = 0;
    if (id_reads && !regs.LUT[60] && fail == 108)
        regs.LUT[60] = 1;
    if (id_reads && !regs.LUT[60] && fail == 109)
        regs.LUT[59] = 1;
    if (id_reads && !regs.LUT[60] && fail == 110)
        regs.LUTCR = 2;
    if (id_reads && !regs.LUT[60] && fail == 111)
        crux_flash_poc_terminal(-ETIMEDOUT);
}
static bool FLEXSPI_GetBusIdleStatus(FLEXSPI_Type *b) {
    if (b->LUTKEY == FLEXSPI_LUT_KEY_VAL)
        updates = 1;
    if (fail == 112 || (id_reads && fail == 113))
        return false;
    return (b->STS0 & 3) == 3;
}
static bool FLEXSPI_CheckInputLutLocation(FLEXSPI_Type *b, uint32_t p) {
    (void)b;
    (void)p;
    return true;
}
static int memc_flexspi_transfer(const struct device *d, flexspi_transfer_t *t) {
    (void)d;
    commands++;
    assert(t->port == 0 && t->SeqNumber == 1);
    if (fail > 0 && fail < 30 && commands == fail)
        return -EIO;
    if (t->seqIndex == 1) {
        statuses++;
        *t->data = busy_polls-- > 0 ? 1U : (wel ? 2U : 0U);
        if (busy_polls == (unsigned)-1)
            busy_polls = 0;
        if (fail == 114)
            *t->data = 1;
        if (id_reads && fail == 115)
            *t->data = 1;
        if (wel && fail == 116)
            *t->data = 0;
        if (wel && fail == 117)
            *t->data = 3;
    } else if (t->seqIndex == 15) {
        assert(regs.LUT[60] == 0x2401049f && regs.LUTCR == 1);
        assert(t->dataSize == 3);
        *t->data = id_value;
        id_reads++;
    } else if (t->seqIndex == 3) {
        assert(regs.LUT[60] == 0 && regs.LUTCR == 1 && id_reads == 1);
        assert(id_value == 0x17709d);
        wel = true;
        writes++;
    } else if (t->seqIndex == 9) {
        assert(wel && t->dataSize == 4 && t->deviceAddress == 0x620000);
        memcpy(programmed, t->data, 4);
        writes++;
        wel = false;
    } else if (t->seqIndex == 5) {
        assert(wel);
        writes++;
        wel = false;
    }
    return 0;
}
static void memc_flexspi_wait_bus_idle(const struct device *d) {
    (void)d;
    idle_calls++;
    if (fail == 118 || (id_reads && fail == 119))
        crux_flash_poc_terminal(-ETIMEDOUT);
}
static int memc_flexspi_reset(const struct device *d) {
    (void)d;
    return fail == 120 ? -EIO : 0;
}
