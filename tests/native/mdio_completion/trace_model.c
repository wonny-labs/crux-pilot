/* Logical first-failure controls; no real MMIO, IRQ, target clock or device. */
#include "network_mdio_trace.h"
#include "register_macros.inc"

#include <assert.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>

_Static_assert(sizeof(struct crux_m3_mdio_failure) == 64, "Unexpected private record layout");
typedef struct {
    volatile uint32_t EIR, MMFR, EIMR, ECR, MSCR;
} ENET_Type;
struct k_mutex {
    int held;
};
struct k_sem {
    unsigned count;
};
struct device {
    void *data;
};
enum mdio_opcode { MDIO_OP_C22_READ = 2 };
#define DEVICE_DT_GET(node) (&device)
#define DT_DRV_INST(index) (index)
#define CONFIG_MDIO_NXP_ENET_TIMEOUT 250
#define K_FOREVER (-1)
#define K_USEC(value) (value)
#include "driver_data.inc"

static ENET_Type registers;
static struct nxp_enet_mdio_data data;
static struct device device = {.data = &data};
static bool locked, complete;
static unsigned starts;
static uint32_t cycles;

static unsigned irq_lock(void)
{
    assert(!locked);
    locked = true;
    return 0;
}
static void irq_unlock(unsigned key)
{
    (void)key;
    assert(locked);
    if (data.first_failure.valid) {
        assert(data.first_failure.clock_hz == 1000000);
        assert(data.first_failure.elapsed_cycles == 0x30);
        assert(data.first_failure.monotonic_ms == 500);
        assert(data.first_failure.result == -ETIMEDOUT);
    }
    locked = false;
}
static uint32_t k_cycle_get_32(void)
{
    uint32_t result = cycles;
    cycles += 0x30;
    return result;
}
static int64_t k_uptime_get(void)
{
    return 500;
}
static uint32_t sys_clock_hw_cycles_per_sec(void)
{
    return 1000000;
}
static unsigned k_sem_count_get(struct k_sem *sem)
{
    assert(locked);
    return sem->count;
}
static bool k_is_in_isr(void)
{
    return false;
}
static int k_mutex_lock(struct k_mutex *mutex, int timeout)
{
    (void)timeout;
    assert(!mutex->held);
    mutex->held = 1;
    return 0;
}
static int k_mutex_unlock(struct k_mutex *mutex)
{
    assert(mutex->held);
    mutex->held = 0;
    return 0;
}
static void k_sem_reset(struct k_sem *sem)
{
    assert(locked && registers.EIR == ENET_EIR_MII_MASK);
    registers.EIR = 0; /* Model the preceding W1C write explicitly. */
    sem->count = 0;
    starts++;
}
static int k_sem_take(struct k_sem *sem, int timeout)
{
    assert(!locked && timeout == 250);
    if (complete) {
        registers.MMFR = (registers.MMFR & ~ENET_MMFR_DATA_MASK) | 0x22;
        sem->count = 1;
    }
    if (sem->count) {
        sem->count--;
        return 0;
    }
    return -EAGAIN;
}
static void k_busy_wait(int us)
{
    assert(us == 1);
    if (complete) {
        registers.MMFR = (registers.MMFR & ~ENET_MMFR_DATA_MASK) | 0x22;
        registers.EIR = ENET_EIR_MII_MASK;
    }
}
#define WAIT_FOR(expr, timeout, delay)                                                             \
    ({                                                                                             \
        unsigned i = 0;                                                                            \
        while (!(expr) && i < (timeout)) {                                                         \
            delay;                                                                                 \
            i++;                                                                                   \
        }                                                                                          \
        !!(expr);                                                                                  \
    })
#include "driver_functions.inc"

static void reset(bool interrupt)
{
    registers = (ENET_Type){.EIMR = 17, .ECR = 33, .MSCR = 49};
    data = (struct nxp_enet_mdio_data){.base = &registers, .interrupt_up = interrupt};
    cycles = 0xfffffff0;
    starts = 0;
    locked = complete = false;
}
static int read_value(uint16_t *value)
{
    return mdio_transfer(&device, 0, 2, MDIO_OP_C22_READ, false, 0, value);
}
static void timeout_case(bool interrupt)
{
    reset(interrupt);
    struct crux_m3_mdio_failure record = {.valid = 0xabcd};
    assert(!crux_m3_mdio_failure_get(&device, &record) && record.valid == 0xabcd);
    uint16_t value = 0xbeef;
    assert(read_value(&value) == -ETIMEDOUT && value == 0xbeef);
    assert(crux_m3_mdio_failure_get(&device, &record));
    assert(record.valid == 1 && record.wait_interrupt == (uint32_t)interrupt);
    assert(record.started_cycles == 0xfffffff0 && record.elapsed_cycles == 0x30);
    assert(record.phy == 0 && record.reg == 2 && record.op == 2);
    assert(record.eir == 0 && record.mmfr == registers.MMFR && record.sem_count == 0);
    assert(record.eimr == 17 && record.ecr == 33 && record.mscr == 49);
    uint32_t original_mmfr = record.mmfr;
    registers.MMFR = 0x12345678;
    data.mdio_sem.count = 1; /* Inject a late old completion; do not clear poison. */
    assert(read_value(&value) == -EIO && starts == 1 && registers.MMFR == 0x12345678);
    assert(crux_m3_mdio_failure_get(&device, &record));
    assert(record.mmfr == original_mmfr && record.sem_count == 0);
    assert(record.wait_interrupt == (uint32_t)interrupt && record.started_cycles == 0xfffffff0);
    assert(!locked && !data.mdio_mutex.held);
}
int main(void)
{
    timeout_case(false);
    timeout_case(true);
    for (unsigned interrupt = 0; interrupt < 2; interrupt++) {
        reset(interrupt != 0);
        complete = true;
        uint16_t value;
        struct crux_m3_mdio_failure record = {.valid = 0xabcd};
        assert(read_value(&value) == 0 && value == 0x22);
        assert(!crux_m3_mdio_failure_get(&device, &record) && record.valid == 0xabcd);
        assert(!crux_m3_mdio_failure_get(NULL, &record));
        assert(!crux_m3_mdio_failure_get(&device, NULL));
    }
    puts("FIRST_FAILURE_LOGICAL_CONTROLS_PASS; no physical timing or cause claim");
    return 0;
}
