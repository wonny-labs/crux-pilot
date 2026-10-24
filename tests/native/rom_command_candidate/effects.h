#include "rom_command.h"
#include <errno.h>
#include <setjmp.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#define JESD216_READ_ID_LEN 3
#define SPI_NOR_WIP_BIT 1U
#define SPI_NOR_WEL_BIT 2U
#define LOG_DBG(...) ((void)0)
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
static bool installed, wel;
bool crux_rom_slot_installed(void) {
    return installed;
}
void crux_rom_identity_check(const struct device *d, bool active) {
    (void)d;
    if (active != installed)
        abort();
}
void crux_rom_identity_install(const struct device *d) {
    (void)d;
    installed = true;
}
void crux_rom_identity_restore(const struct device *d) {
    (void)d;
    installed = false;
}
static int failure, transfers, polls, ticks, idle_calls, reason, programmed;
static char trace[4096];
static size_t used;
static jmp_buf terminal;
static int crux_rom_memc_bind_effect(struct device *d) {
    (void)d;
    return 0;
}
int crux_rom_memc_bind(struct device *d) {
    return crux_rom_memc_bind_effect(d);
}
_Noreturn void crux_flash_poc_terminal(int e) {
    reason = e;
    longjmp(terminal, 1);
}
bool crux_rom_elapsed_expired(void) {
    ++ticks;
    return failure == 6 || (failure == 16 && ticks == 10) || ticks > 60;
}
static void event(char c) {
    trace[used++] = c;
    trace[used++] = ',';
    trace[used] = 0;
}
static int memc_flexspi_transfer(const struct device *d, flexspi_transfer_t *t) {
    (void)d;
    event('0' + t->seqIndex);
    transfers++;
    if (t->port != 0 || t->SeqNumber != 1)
        abort();
    if (t->seqIndex == 9) {
        programmed = ((uint8_t *)t->data)[0];
        if (t->deviceAddress != 0x620000 || t->dataSize != 4)
            abort();
    }
    if (failure == transfers && failure < 5)
        return -EIO;
    if ((failure >= 10 && failure <= 14 && transfers == failure - 9) ||
        (failure >= 30 && failure <= 32 && transfers == failure - 24))
        return -EIO;
    if (t->seqIndex == 1) {
        polls++;
        *t->data = (failure == 5 || (failure == 15 && polls >= 5) || polls == 1)
                       ? 1
                       : (wel ? SPI_NOR_WEL_BIT : 0);
    }
    if (t->seqIndex == 15)
        *t->data = 0x17709d;
    if (t->seqIndex == 3)
        wel = true;
    if (t->seqIndex == 9 || t->seqIndex == 5)
        wel = false;
    return 0;
}
static void memc_flexspi_wait_bus_idle(const struct device *d) {
    (void)d;
    event('I');
    idle_calls++;
    if (failure == 7 || (failure == 9 && idle_calls == 3) || (failure == 24 && idle_calls == 4))
        crux_flash_poc_terminal(-ETIMEDOUT);
}
static int memc_flexspi_reset(const struct device *d) {
    (void)d;
    event('R');
    return failure == 8 ? -EIO : 0;
}
