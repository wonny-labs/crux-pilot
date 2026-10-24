#include <errno.h>
#include <setjmp.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define MHZ(x) ((x) * 1000000U)
#define FLEXSPI_FLSHCR0_FLSHSZ_MASK 8388607U
#define READ 0
#define JESD216_SFDP_SIZE(x) 16
#define JESD216_SFDP_MAGIC 0x50444653U
#define ARRAY_SIZE(x) (sizeof(x) / sizeof((x)[0]))
#define MEMC_FLEXSPI_CMD_PER_SEQ 4
#define FLEXSPI_INSTR_END 8
#define FLEXSPI_INSTR_PROG_END 10
#define __aligned(x) __attribute__((aligned(x)))
#define LOG_DBG(...) ((void)0)
#define LOG_WRN(...) ((void)0)
struct device {
    void *data;
    const void *config;
};
typedef struct {
    unsigned flexspiRootClk, flashSize, ARDSeqNumber, ARDSeqIndex;
} flexspi_device_config_t;
struct flash_flexspi_nor_data {
    struct device controller;
    unsigned port;
    flexspi_device_config_t config;
    bool legacy_poll;
};
struct jesd216_bfp {
    unsigned unused;
};
struct jesd216_param_header {
    unsigned len_dw;
};
struct jesd216_sfdp_header {
    unsigned magic;
    struct jesd216_param_header phdr[1];
};
static uint32_t flexspi_probe_lut[64], flash_flexspi_nor_base_lut[64];
static int stage, config_calls, sfdp_calls, resets, unlocks, reason, result = 777;
static jmp_buf terminal;
static _Noreturn void crux_flash_poc_terminal(int error) {
    reason = error;
    longjmp(terminal, 1);
}
static bool memc_flexspi_is_running_xip(const struct device *d) {
    (void)d;
    return true;
}
static unsigned irq_lock(void) {
    return 0;
}
static void irq_unlock(unsigned key) {
    (void)key;
    unlocks++;
}
static void memc_flexspi_wait_bus_idle(const struct device *d) {
    (void)d;
}
static void memc_flexspi_reset(const struct device *d) {
    (void)d;
    resets++;
}
static int memc_flexspi_update_clock(const struct device *d, flexspi_device_config_t *c, unsigned p,
                                     unsigned f) {
    (void)d;
    (void)c;
    (void)p;
    (void)f;
    return stage == 1 ? -EIO : 0;
}
static int memc_flexspi_set_device_config(const struct device *d, const flexspi_device_config_t *c,
                                          const uint32_t *l, unsigned n, unsigned p) {
    (void)d;
    (void)c;
    (void)l;
    (void)n;
    (void)p;
    config_calls++;
    return (stage == 2 && config_calls == 1) || (stage == 8 && config_calls == 2) ? -EIO : 0;
}
static int flash_flexspi_nor_check_jedec(struct flash_flexspi_nor_data *d, uint32_t *l) {
    (void)d;
    (void)l;
    return stage == 3 ? -EIO : stage >= 4 && stage != 8 ? -ENOTSUP : 0;
}
static int flash_flexspi_nor_sfdp_read_helper(struct flash_flexspi_nor_data *d, unsigned a, void *b,
                                              size_t n) {
    (void)d;
    (void)a;
    (void)n;
    sfdp_calls++;
    if ((stage == 4 && sfdp_calls == 1) || (stage == 6 && sfdp_calls == 2))
        return -EIO;
    struct jesd216_sfdp_header *h = b;
    h->magic = JESD216_SFDP_MAGIC;
    h->phdr[0].len_dw = stage == 5 ? 24 : 1;
    return 0;
}
static uint32_t jesd216_sfdp_magic(struct jesd216_sfdp_header *h) {
    return h->magic;
}
static unsigned jesd216_param_addr(struct jesd216_param_header *h) {
    (void)h;
    return 0;
}
static int flash_flexspi_nor_config_flash(struct flash_flexspi_nor_data *d,
                                          struct jesd216_sfdp_header *h, struct jesd216_bfp *b,
                                          uint32_t *l) {
    (void)d;
    (void)h;
    (void)b;
    (void)l;
    return stage == 7 ? -EIO : 0;
}
