#include <assert.h>
#include <string.h>
enum { kFLEXSPI_PortA1 };
static unsigned waits;
static flexspi_transfer_t *changing;
void crux_rom_candidate_wait_check(void) {
    waits++;
    if (changing)
        changing->cmdType = kFLEXSPI_Write;
}
static status_t FLEXSPI_ReadBlocking(FLEXSPI_Type *base, uint8_t *bytes, size_t length) {
    (void)base;
    (void)bytes;
    assert(length == 1 || length == 3);
    return 0;
}
static status_t FLEXSPI_WriteBlocking(FLEXSPI_Type *base, const uint8_t *bytes, size_t length) {
    (void)base;
    (void)bytes;
    (void)length;
    assert(0);
    return -1;
}
