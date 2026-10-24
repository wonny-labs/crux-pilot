#include <errno.h>
#include <setjmp.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
typedef int status_t;
enum {
    kStatus_Success = 0,
    kFLEXSPI_IpTxFifoWatermarkEmptyFlag = 1,
    kFLEXSPI_IpRxFifoWatermarkAvailableFlag = 2,
    kFLEXSPI_Read = 1,
    kFLEXSPI_Write = 2,
    kFLEXSPI_Config = 3
};
#define FLEXSPI_IPTXFCR_TXWMRK_MASK 0xff
#define FLEXSPI_IPTXFCR_TXWMRK_SHIFT 0
#define FLEXSPI_IPRXFCR_RXWMRK_MASK 0xff
#define FLEXSPI_IPRXFCR_RXWMRK_SHIFT 0
#define FLEXSPI_IPRXFSTS_FILL_MASK 0xff
#define FLEXSPI_IPRXFSTS_FILL_SHIFT 0
#define FLEXSPI_FLSHCR2_CLRINSTRPTR_MASK 1
#define FLEXSPI_INTR_AHBCMDERR_MASK 16
#define FLEXSPI_INTR_IPCMDERR_MASK 32
#define FLEXSPI_INTR_AHBCMDGE_MASK 64
#define FLEXSPI_INTR_IPCMDGE_MASK 128
#define FLEXSPI_INTR_IPCMDDONE_MASK 4
#define FLEXSPI_IPTXFCR_CLRIPTXF_MASK 256
#define FLEXSPI_IPRXFCR_CLRIPRXF_MASK 256
#define FLEXSPI_IPCR1_IDATSZ(x) (x)
#define FLEXSPI_IPCR1_ISEQID(x) ((x) << 16)
#define FLEXSPI_IPCR1_ISEQNUM(x) ((x) << 24)
#define FLEXSPI_IPCMD_TRG_MASK 1
static int mode, reason;
static unsigned accesses, idles;
/* Script reads independently from write-one-to-clear stores. This is not a register simulator. */
static unsigned intr_cell(void) {
    unsigned n = accesses++;
    return mode == 3 || mode == 8 ? (n == 0 ? 0 : 1) : 1;
}
#define INTR intr[intr_cell()]
#define IPRXFSTS fill[accesses++, 0]
typedef struct {
    volatile uint32_t IPTXFCR, IPRXFCR, intr[2], fill[1], TFDR[16], RFDR[16], FLSHCR2[4], IPCR0,
        IPCR1, IPCMD;
} FLEXSPI_Type;
typedef struct {
    unsigned port, deviceAddress, cmdType, dataSize, seqIndex, SeqNumber;
    void *data;
} flexspi_transfer_t;
static bool FLEXSPI_GetBusIdleStatus(FLEXSPI_Type *b) {
    (void)b;
    idles++;
    return mode != 4 && (mode != 5 || idles == 1);
}
static status_t FLEXSPI_CheckAndClearError(FLEXSPI_Type *b, uint32_t status) {
    (void)b;
    (void)status;
    return mode == 9 || mode == 10 ? -1 : 0;
}
static jmp_buf terminal;
static _Noreturn void crux_flash_poc_terminal(int error) {
    reason = error;
    longjmp(terminal, 1);
}
