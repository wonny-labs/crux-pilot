/* SPDX-License-Identifier: Apache-2.0 */
/*
 * Task identifiers: a non-zero 32-bit value per thread, the same on every native_sim width, taken
 * from a small table rather than from a truncated pointer. The adapter has no hook for thread exit,
 * so a thread keeps its slot and the table bounds the threads that ever use the file system.
 */
#include <stdint.h>
#include <zephyr/kernel.h>
#include <zephyr/spinlock.h>

#include <redfs.h>

/* Every thread without a slot gets this one value. Reliance Edge cannot register it while its own
 * table, of the same size, is full, so the call fails with RED_EUSERS. */
#define CRUX_RED_TASK_OVERFLOW ((uint32_t)REDCONF_TASK_COUNT + 1U)

static k_tid_t crux_red_tasks[REDCONF_TASK_COUNT];
/* RedOsTaskId is also called outside the file system mutex, so the table has its own lock. */
static struct k_spinlock crux_red_task_lock;

uint32_t RedOsTaskId(void)
{
    k_tid_t self = k_current_get();
    uint32_t id = CRUX_RED_TASK_OVERFLOW;
    size_t free_slot = ARRAY_SIZE(crux_red_tasks);
    k_spinlock_key_t key = k_spin_lock(&crux_red_task_lock);

    for (size_t i = 0; i < ARRAY_SIZE(crux_red_tasks); ++i) {
        if (crux_red_tasks[i] == self) {
            id = (uint32_t)i + 1U;
            break;
        }
        if (crux_red_tasks[i] == NULL && free_slot == ARRAY_SIZE(crux_red_tasks)) {
            free_slot = i;
        }
    }
    if (id == CRUX_RED_TASK_OVERFLOW && free_slot < ARRAY_SIZE(crux_red_tasks)) {
        crux_red_tasks[free_slot] = self;
        id = (uint32_t)free_slot + 1U;
    }
    k_spin_unlock(&crux_red_task_lock, key);
    return id;
}
