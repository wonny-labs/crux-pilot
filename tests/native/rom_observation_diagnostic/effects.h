/* Controlled device effects; all admission, sequence, slot and result policy is actual C. */
#include "observation.h"
#include "rom_command.h"
#include <assert.h>
#include <setjmp.h>
#include <stdlib.h>
#include <string.h>
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
    uint32_t LUT[64], LUTKEY, LUTCR, STS0, MCR0, MCR2, FLSHCR0[4], FLSHCR1[4], FLSHCR2[4];
} FLEXSPI_Type;
volatile struct crux_observation_record crux_observation_result;
static FLEXSPI_Type regs;
static jmp_buf terminal;
static int reason, fail, transfers, statuses, identities, finishes, wait_checks;
static uint32_t id_value = 0x17709d;
static FLEXSPI_Type *get_base(const struct device *dev) {
    (void)dev;
    return &regs;
}
int crux_rom_memc_bind(struct device *dev) {
    (void)dev;
    return fail == 1 ? -EACCES : 0;
}
_Noreturn void crux_flash_poc_terminal(int error) {
    reason = error;
    crux_observation_result.result = error;
    crux_observation_result.phase = CRUX_OBSERVATION_CONTAINED;
    crux_observation_result.disposition = CRUX_OBSERVATION_RAM_CONTAINED;
    longjmp(terminal, 1);
}
void crux_rom_candidate_wait_check(void) {
    wait_checks++;
    if (fail == 2 || (identities && fail == 10))
        crux_flash_poc_terminal(-ETIMEDOUT);
    if (identities && fail == 11)
        regs.LUT[2] ^= 1;
}
void crux_rom_candidate_deadline_finish(void) {
    finishes++;
    crux_rom_candidate_wait_check();
}
static bool FLEXSPI_GetBusIdleStatus(FLEXSPI_Type *base) {
    return (base->STS0 & 3) == 3;
}
static bool FLEXSPI_CheckInputLutLocation(FLEXSPI_Type *base, uint32_t position) {
    (void)base;
    (void)position;
    return true;
}
static int memc_flexspi_transfer(const struct device *dev, flexspi_transfer_t *transfer) {
    (void)dev;
    transfers++;
    assert(transfer->deviceAddress == 0 && transfer->port == kFLEXSPI_PortA1);
    assert(transfer->cmdType == kFLEXSPI_Read && transfer->SeqNumber == 1);
    if (fail == 3 || (identities && fail == 8))
        return -EIO;
    if (transfer->seqIndex == 1) {
        assert(transfer->dataSize == 1);
        statuses++;
        *transfer->data = (fail == 4 || (identities && fail == 9)) ? 1
                          : (fail == 5 || fail == 12)              ? 2
                                                                   : 0;
        if (fail == 12)
            regs.LUT[2] ^= 1;
    } else {
        assert(transfer->seqIndex == 15 && transfer->dataSize == 3);
        assert(regs.LUT[60] == 0x2401049f && regs.LUTCR == 1);
        identities++;
        if (fail == 7)
            return -EIO;
        *transfer->data = id_value;
    }
    return 0;
}
static void memc_flexspi_wait_bus_idle(const struct device *dev) {
    (void)dev;
    crux_rom_candidate_wait_check();
    if (fail == 6)
        crux_flash_poc_terminal(-ETIMEDOUT);
}
