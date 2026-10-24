#include <errno.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define LOG_DBG(...) ((void)0)
#define BIT(n) (1U << (n))
#define MHZ(x) ((x) * 1000000U)
#define FLEXSPI_FLSHCR0_FLSHSZ_MASK 8388607
#define MEMC_FLEXSPI_CMD_PER_SEQ 4
#define FLEXSPI_INSTR_END 8
#define FLEXSPI_LUT_SEQ(a, b, c, d, e, f) ((uint32_t)((a) + (b) + (c) + (d) + (e) + (f)))
#define READ 0
#define READ_STATUS_REG 1
#define ERASE_SECTOR 2
#define ERASE_BLOCK 3
#define PAGE_PROGRAM 4
#define SCRATCH_CMD 5
#define SCRATCH_CMD2 6
#define JESD216_DW19_OER_VAL_NONE 0
#define JESD216_SFDP_BFP_DW1_ADDRBYTES_VAL_4B 1
#define JESD216_MODE_188 188
#define JESD216_MODE_144 144
#define JESD216_MODE_122 122
struct device {
    int unused;
};
typedef struct {
    unsigned flexspiRootClk, flashSize, ARDSeqNumber, ARDSeqIndex;
} flexspi_device_config_t;
typedef struct {
    unsigned deviceAddress, port, seqIndex, SeqNumber, dataSize, cmdType;
    uint32_t *data;
} flexspi_transfer_t;
struct flash_flexspi_nor_data {
    struct device controller;
    unsigned port;
    bool legacy_poll;
    uint8_t jedec_id[3];
};
struct jesd216_param_header {
    unsigned unused;
};
struct jesd216_sfdp_header {
    struct jesd216_param_header phdr[1];
};
struct jesd216_bfp {
    unsigned unused;
};
struct jesd216_instr {
    unsigned mode_clocks, instr, wait_states;
};
struct jesd216_bfp_dw14 {
    unsigned poll_options;
};
struct jesd216_bfp_dw15 {
    unsigned qer;
};
struct jesd216_bfp_dw16 {
    unsigned enter_4ba;
};
struct jesd216_bfp_dw19 {
    unsigned octal_enable_req;
};
static int mode, transfers, configs, quad_calls, wren_calls;
static int jesd216_bfp_decode_dw14(struct jesd216_param_header *h, struct jesd216_bfp *b,
                                   struct jesd216_bfp_dw14 *v) {
    (void)h;
    (void)b;
    v->poll_options = 0;
    return 0;
}
static int jesd216_bfp_decode_dw15(struct jesd216_param_header *h, struct jesd216_bfp *b,
                                   struct jesd216_bfp_dw15 *v) {
    (void)h;
    (void)b;
    v->qer = 0;
    return 0;
}
static int jesd216_bfp_decode_dw16(struct jesd216_param_header *h, struct jesd216_bfp *b,
                                   struct jesd216_bfp_dw16 *v) {
    (void)h;
    (void)b;
    v->enter_4ba = 0;
    return 0;
}
static int jesd216_bfp_decode_dw19(struct jesd216_param_header *h, struct jesd216_bfp *b,
                                   struct jesd216_bfp_dw19 *v) {
    (void)h;
    (void)b;
    v->octal_enable_req = 0;
    return 0;
}
static int jesd216_bfp_addrbytes(struct jesd216_bfp *b) {
    (void)b;
    return 0;
}
static int jesd216_bfp_read_support(struct jesd216_param_header *h, struct jesd216_bfp *b,
                                    unsigned m, struct jesd216_instr *v) {
    (void)h;
    (void)b;
    v->mode_clocks = 0;
    v->instr = 0;
    v->wait_states = 0;
    return m == JESD216_MODE_144 ? 1 : 0;
}
static int flash_flexspi_nor_4byte_enable(struct flash_flexspi_nor_data *d, uint32_t (*l)[4],
                                          unsigned m) {
    (void)d;
    (void)l;
    (void)m;
    return mode == 1 ? -EIO : mode == 2 ? -ENOTSUP : 0;
}
static int flash_flexspi_nor_quad_enable(struct flash_flexspi_nor_data *d, uint32_t (*l)[4],
                                         unsigned m) {
    (void)d;
    (void)l;
    (void)m;
    quad_calls++;
    return mode == 3 ? -EIO : mode == 4 ? -ENOTSUP : 0;
}
static int flash_flexspi_nor_handle_octal_requirements(struct flash_flexspi_nor_data *d,
                                                       uint32_t (*l)[4], unsigned m) {
    (void)d;
    (void)l;
    (void)m;
    return 0;
}
static int memc_flexspi_set_device_config(const struct device *d, const flexspi_device_config_t *c,
                                          const uint32_t *l, unsigned n, unsigned p) {
    (void)d;
    (void)c;
    (void)l;
    (void)n;
    (void)p;
    configs++;
    return 0;
}
static int memc_flexspi_transfer(const struct device *d, flexspi_transfer_t *t) {
    (void)d;
    (void)t;
    transfers++;
    return mode == 5 && transfers == 1 ? -EIO : 0;
}
static int flash_flexspi_nor_write_enable(struct flash_flexspi_nor_data *d) {
    (void)d;
    wren_calls++;
    return mode == 6 ? -EIO : 0;
}

static int octal_calls;
static int flash_flexspi_nor_octal_enable(struct flash_flexspi_nor_data *d, uint32_t (*l)[4]) {
    (void)d;
    (void)l;
    octal_calls++;
    return mode == 7 ? -EIO : 0;
}
static int flash_flexspi_nor_read_id_helper(struct flash_flexspi_nor_data *d, uint8_t *b) {
    (void)d;
    uint32_t id = mode == 10 ? 0x123456 : 0x195b2c;
    memcpy(b, &id, 4);
    return mode == 9 ? -EIO : 0;
}
static int flash_flexspi_nor_is25_clear_dummy_cycles(struct flash_flexspi_nor_data *d,
                                                     uint32_t (*l)[4]) {
    (void)d;
    (void)l;
    return 0;
}
static _Noreturn void crux_flash_poc_terminal(int error) {
    (void)error;
    abort();
}
#define LOG_ERR(...) ((void)0)
#define JESD216_DW15_QER_VAL_S2B1v5 5
#define JESD216_DW15_QER_VAL_S1B6 6
#define JESD216_DW15_QER_VAL_S2B1v6 7
#define WRITE_REG 7
#define WRITE_ENABLE 8
