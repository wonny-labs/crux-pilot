#include "rom_command.h"
#include <errno.h>
#include <setjmp.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
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
    return failure == 6 || (failure == 16 && ticks == 6) || ticks > 12;
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
    if (failure >= 10 && failure <= 14 && transfers == failure - 9)
        return -EIO;
    if (t->seqIndex == 1) {
        polls++;
        *t->data = (failure == 5 || (failure == 15 && polls >= 3) || polls == 1) ? 1 : 0;
    }
    return 0;
}
static void memc_flexspi_wait_bus_idle(const struct device *d) {
    (void)d;
    event('I');
    idle_calls++;
    if (failure == 7 || (failure == 9 && idle_calls == 2) || (failure == 24 && idle_calls == 3))
        crux_flash_poc_terminal(-ETIMEDOUT);
}
static int memc_flexspi_reset(const struct device *d) {
    (void)d;
    event('R');
    return failure == 8 ? -EIO : 0;
}
