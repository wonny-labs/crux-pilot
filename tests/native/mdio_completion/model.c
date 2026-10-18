/* Logical schedules only: fake MMIO/IRQ and semaphore operations, no hardware. */
#include "register_macros.inc"
#include <assert.h>
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
typedef struct {
    volatile uint32_t EIR, MMFR;
} ENET_Type;
struct k_mutex {
    int held;
};
struct k_sem {
    int count;
};
#include "driver_data.inc"
struct device {
    void *data;
};
enum mdio_opcode { MDIO_OP_C22_READ = 2 };
#define CONFIG_MDIO_NXP_ENET_TIMEOUT 250
#define K_FOREVER (-1)
#define K_USEC(x) (x)
static ENET_Type registers;
static struct nxp_enet_mdio_data data;
static bool isr, locked, complete_on_wait, late_on_timeout, poll_completion, stale_pending,
    complete_before_unlock;
static unsigned starts, takes, polls, irqs;
static void completion(void) {
    registers.MMFR = (registers.MMFR & ~ENET_MMFR_DATA_MASK) | 0x22;
    registers.EIR = ENET_EIR_MII_MASK;
    if (data.interrupt_up) {
        assert(!locked);
        registers.EIR = 0;
        data.mdio_sem.count = 1;
        irqs++;
    }
}
static bool k_is_in_isr(void) {
    return isr;
}
static unsigned irq_lock(void) {
    assert(!locked);
    locked = true;
    return 0;
}
static void irq_unlock(unsigned key) {
    (void)key;
    assert(locked && registers.EIR == 0);
    if (complete_before_unlock) {
        registers.MMFR = (registers.MMFR & ~ENET_MMFR_DATA_MASK) | 0x22;
        registers.EIR = ENET_EIR_MII_MASK;
    }
    locked = false;
    starts++;
    if (complete_before_unlock && data.interrupt_up) {
        assert(registers.EIR & ENET_EIR_MII_MASK);
        registers.EIR = 0;
        data.mdio_sem.count = 1;
        irqs++;
    }
    /* A pending old NVIC request sees cleared EIR: it cannot give a token. */
    if (stale_pending) {
        assert(!(registers.EIR & ENET_EIR_MII_MASK));
        stale_pending = false;
    }
}
static void k_sem_reset(struct k_sem *s) {
    assert(locked && registers.EIR == ENET_EIR_MII_MASK);
    registers.EIR = 0; /* Explicit model of the preceding hardware W1C write. */
    s->count = 0;
}
static void k_busy_wait(int us) {
    assert(us == 1);
    polls++;
    if (poll_completion && polls == 3)
        completion();
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
static int k_mutex_lock(struct k_mutex *m, int timeout) {
    (void)timeout;
    assert(!m->held && !isr);
    m->held = 1;
    return 0;
}
static int k_mutex_unlock(struct k_mutex *m) {
    assert(m->held);
    m->held = 0;
    return 0;
}
static int k_sem_take(struct k_sem *s, int timeout) {
    assert(timeout == 250 && !locked);
    takes++;
    if (complete_on_wait)
        completion();
    if (s->count) {
        s->count--;
        return 0;
    }
    if (late_on_timeout)
        completion(); /* Completion loses the kernel timeout race. */
    return -EAGAIN;
}
#include "candidate_fragments.inc"
static void reset(bool irq) {
    registers = (ENET_Type){0};
    data = (struct nxp_enet_mdio_data){.base = &registers, .interrupt_up = irq};
    isr = locked = complete_on_wait = late_on_timeout = poll_completion = stale_pending =
        complete_before_unlock = false;
    starts = takes = polls = irqs = 0;
}
static int read_value(uint16_t *value) {
    const struct device dev = {.data = &data};
    return mdio_transfer(&dev, 0, 2, MDIO_OP_C22_READ, false, 0, value);
}
static void no_more_commands(void) {
    unsigned before = starts;
    uint32_t mmfr = registers.MMFR;
    uint16_t value = 0xbeef;
    assert(read_value(&value) == -EIO && starts == before && registers.MMFR == mmfr &&
           value == 0xbeef);
    assert(!data.mdio_mutex.held && !locked);
}
int main(void) {
    uint16_t value;
    reset(true);
    data.mdio_sem.count = 1;
    value = 0xbeef;
    assert(read_value(&value) == -ETIMEDOUT && value == 0xbeef && data.xfer_failed);
    no_more_commands();
    puts("PASS old token drained; absent completion times out; later writes refused");
    reset(true);
    late_on_timeout = true;
    value = 0xbeef;
    assert(read_value(&value) == -ETIMEDOUT && irqs == 1 && data.mdio_sem.count == 1 &&
           data.xfer_failed && value == 0xbeef);
    no_more_commands();
    puts("PASS completion losing timeout race cannot satisfy a later transaction");
    reset(true);
    value = 0xbeef;
    assert(read_value(&value) == -ETIMEDOUT);
    completion();
    no_more_commands();
    puts("PASS IRQ after timeout/poison cannot enable another command");
    reset(true);
    stale_pending = true;
    complete_on_wait = true;
    assert(read_value(&value) == 0 && value == 0x22 && irqs == 1 && !data.xfer_failed);
    assert(read_value(&value) == 0 && value == 0x22 && starts == 2 && takes == 2 && irqs == 2);
    puts("PASS stale pending IRQ rejected; fresh success and next transaction succeed");
    reset(true);
    complete_before_unlock = true;
    value = 0xbeef;
    assert(read_value(&value) == 0 && value == 0x22 && irqs == 1 && starts == 1 &&
           !data.xfer_failed);
    puts("PASS new completion while IRQ locked is accepted after unlock");
    reset(false);
    poll_completion = true;
    assert(read_value(&value) == 0 && value == 0x22 && takes == 0 && polls == 3 &&
           !data.xfer_failed);
    puts("PASS initial polling requires an actual completion indication");
    reset(false);
    value = 0xbeef;
    assert(read_value(&value) == -ETIMEDOUT && value == 0xbeef && polls == 250 && takes == 0);
    no_more_commands();
    puts("PASS initial polling timeout preserves output and blocks later commands");
    reset(true);
    isr = true;
    value = 0xbeef;
    assert(read_value(&value) == -EWOULDBLOCK && starts == 0 && !data.mdio_mutex.held &&
           value == 0xbeef);
    puts("PASS interrupt context rejected before mutex or register writes");
    puts("CANDIDATE_LOGICAL_CONTROLS_PASS; physical cause, timing and correction unqualified");
    return 0;
}
