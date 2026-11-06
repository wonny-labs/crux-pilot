/* SPDX-License-Identifier: Apache-2.0 */
/*
 * Task identifiers: a non-zero 32-bit value per thread, the same on every native_sim width, taken
 * from a small table rather than from a truncated pointer. The adapter has no hook for thread exit,
 * so a thread keeps its slot and the table bounds the threads that ever use the file system.
 *
 * A thread takes its slot when it first calls the adapter, before any Reliance Edge call. One
 * without a slot is refused there, so Reliance Edge only ever asks for the identifier of a thread
 * that has one.
 */
#include "fs_reliance_edge_priv.h"

#include <stdint.h>
#include <zephyr/kernel.h>
#include <zephyr/spinlock.h>

#include <redfs.h>

/* What RedOsTaskId returns for a thread without a slot, if it is asked directly. The adapter never
 * lets such a thread reach Reliance Edge, which is the only other caller. */
#define CRUX_RED_TASK_NO_SLOT ((uint32_t)REDCONF_TASK_COUNT + 1U)

static k_tid_t crux_red_tasks[REDCONF_TASK_COUNT];
/* The table is read outside the file system mutex, so it has its own lock. */
static struct k_spinlock crux_red_task_lock;

/* The slot number of the calling thread, from 1, taking a free slot if it has none. Zero when every
 * slot belongs to another thread. */
static uint32_t crux_red_task_slot(void)
{
    k_tid_t self = k_current_get();
    uint32_t slot = 0U;
    size_t free_slot = ARRAY_SIZE(crux_red_tasks);
    k_spinlock_key_t key = k_spin_lock(&crux_red_task_lock);

    for (size_t i = 0; i < ARRAY_SIZE(crux_red_tasks); ++i) {
        if (crux_red_tasks[i] == self) {
            slot = (uint32_t)i + 1U;
            break;
        }
        if (crux_red_tasks[i] == NULL && free_slot == ARRAY_SIZE(crux_red_tasks)) {
            free_slot = i;
        }
    }
    if (slot == 0U && free_slot < ARRAY_SIZE(crux_red_tasks)) {
        crux_red_tasks[free_slot] = self;
        slot = (uint32_t)free_slot + 1U;
    }
    k_spin_unlock(&crux_red_task_lock, key);
    return slot;
}

bool crux_red_task_enter(void)
{
    return crux_red_task_slot() != 0U;
}

uint32_t RedOsTaskId(void)
{
    uint32_t slot = crux_red_task_slot();

    return (slot != 0U) ? slot : CRUX_RED_TASK_NO_SLOT;
}
