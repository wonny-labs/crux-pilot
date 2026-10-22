
#include <errno.h>
#include <setjmp.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#define ALWAYS_INLINE inline
#define LOG_ERR(...) ((void)0)
#define LOG_DBG(...) ((void)0)
#define BIT(n) (1U << (n))
#define MIN(a, b) ((a) < (b) ? (a) : (b))
#define KB(n) ((n) * 1024U)
#define SPI_NOR_PAGE_SIZE 256U
#define SPI_NOR_SECTOR_SIZE 4096U
#define SPI_NOR_BLOCK_SIZE 65536U
struct device {
    void *data;
};
struct flash_flexspi_nor_data {
    struct device controller;
    unsigned port;
    uint64_t size;
    bool legacy_poll;
    struct {
        unsigned flashSize;
    } config;
};
static int fail_stage, calls[6], xip, unlocks, resets, fail_after = 1;
static jmp_buf terminal;
static int terminal_reason;
static _Noreturn void crux_flash_poc_terminal(int reason) {
    terminal_reason = reason;
    longjmp(terminal, 1);
}
static int effect(int stage) {
    calls[stage]++;
    return fail_stage == stage && calls[stage] == fail_after ? -EIO : 0;
}
static void *memc_flexspi_get_ahb_address(const struct device *d, unsigned p, off_t o) {
    (void)d;
    (void)p;
    (void)o;
    return (void *)1;
}
static bool memc_flexspi_is_running_xip(const struct device *d) {
    (void)d;
    return xip;
}
static unsigned irq_lock(void) {
    return 0;
}
static void irq_unlock(unsigned k) {
    (void)k;
    unlocks++;
}
static void memc_flexspi_wait_bus_idle(const struct device *d) {
    (void)d;
}
static void memc_flexspi_reset(const struct device *d) {
    (void)d;
    resets++;
}
static int flash_flexspi_nor_write_enable(struct flash_flexspi_nor_data *d) {
    (void)d;
    return effect(1);
}
static int flash_flexspi_nor_page_program(struct flash_flexspi_nor_data *d, off_t o, const void *b,
                                          size_t n) {
    (void)d;
    (void)o;
    (void)b;
    (void)n;
    return effect(2);
}
static int flash_flexspi_nor_erase_sector(struct flash_flexspi_nor_data *d, off_t o) {
    (void)d;
    (void)o;
    return effect(2);
}
static int flash_flexspi_nor_erase_block(struct flash_flexspi_nor_data *d, off_t o) {
    (void)d;
    (void)o;
    return effect(2);
}
static int flash_flexspi_nor_erase_chip(struct flash_flexspi_nor_data *d) {
    (void)d;
    return effect(2);
}
static int flash_flexspi_nor_read_status(struct flash_flexspi_nor_data *d, uint32_t *s) {
    (void)d;
    *s = fail_stage == 4 ? 1 : 0;
    return effect(3);
}
