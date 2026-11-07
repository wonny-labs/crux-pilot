/* SPDX-License-Identifier: Apache-2.0 */
/*
 * Task identifiers: a non-zero 32-bit value per thread, the same on every native_sim width, taken
 * from a small table rather than from a truncated pointer. The adapter has no hook for thread exit,
 * so a thread keeps its slot and the table bounds the threads that ever use the file system.
 *
 * A thread takes its slot when it first calls the adapter, before any Reliance Edge call. One
 * without a slot is refused there, so that it cannot take a registration of Reliance Edge's own
 * table from a thread that has a slot. The one exception is a close: a handle may be closed from
 * any thread, and a handle that is never closed keeps the volume from being unmounted. Reliance
 * Edge's table therefore has one entry more than the adapter's, kept for one identifier that a
 * thread without a slot holds while it closes a file or a directory. A mutex lets one such thread
 * hold it at a time, so two of them never share the identifier or its error number.
 */
#include "fs_reliance_edge_priv.h"

#include <stdint.h>
#include <zephyr/kernel.h>
#include <zephyr/spinlock.h>

#include <redfs.h>

#define CRUX_RED_SLOTS CONFIG_CRUX_FS_RELIANCE_EDGE_TASK_COUNT

/* A replaced configuration that drops the spare entry would let the close of a thread without a
 * slot take a registration from a thread that has one. */
#if REDCONF_TASK_COUNT != CRUX_RED_SLOTS + 1
#error "REDCONF_TASK_COUNT must be CONFIG_CRUX_FS_RELIANCE_EDGE_TASK_COUNT + 1"
#endif

/* The identifier of the thread that holds the spare entry, during its close. */
#define CRUX_RED_TASK_SPARE ((uint32_t)CRUX_RED_SLOTS + 1U)
/* What RedOsTaskId returns, if it is asked directly, for any other thread without a slot. The
 * adapter never lets such a thread reach Reliance Edge, which is the only other caller. */
#define CRUX_RED_TASK_NO_SLOT ((uint32_t)CRUX_RED_SLOTS + 2U)

static k_tid_t crux_red_tasks[CRUX_RED_SLOTS];
/* The thread that holds the spare identifier, or NULL. Set and cleared under the spinlock, and
 * changed only by a thread that holds crux_red_spare_lock. */
static k_tid_t crux_red_spare_holder;
/* The table is read outside the file system mutex, so it has its own lock. */
static struct k_spinlock crux_red_task_lock;
/* Taken before any other lock of the adapter or of Reliance Edge, and only by a close or closedir
 * of a thread without a slot, so a thread with a slot never waits for it. */
static K_MUTEX_DEFINE(crux_red_spare_lock);

/* The slot number of the calling thread, from 1, taking a free slot if it has none. Zero when every
 * slot belongs to another thread. Called with the spinlock held. */
static uint32_t crux_red_task_slot_locked(k_tid_t self)
{
    size_t free_slot = ARRAY_SIZE(crux_red_tasks);

    for (size_t i = 0; i < ARRAY_SIZE(crux_red_tasks); ++i) {
        if (crux_red_tasks[i] == self) {
            return (uint32_t)i + 1U;
        }
        if (crux_red_tasks[i] == NULL && free_slot == ARRAY_SIZE(crux_red_tasks)) {
            free_slot = i;
        }
    }
    if (free_slot < ARRAY_SIZE(crux_red_tasks)) {
        crux_red_tasks[free_slot] = self;
        return (uint32_t)free_slot + 1U;
    }
    return 0U;
}

bool crux_red_task_enter(void)
{
    k_spinlock_key_t key = k_spin_lock(&crux_red_task_lock);
    uint32_t slot = crux_red_task_slot_locked(k_current_get());

    k_spin_unlock(&crux_red_task_lock, key);
    return slot != 0U;
}

bool crux_red_spare_take(void)
{
    if (crux_red_task_enter()) {
        return false;
    }
    /* A slot is never freed, so the thread still has none once it holds the mutex. */
    if (k_mutex_lock(&crux_red_spare_lock, K_FOREVER) != 0) {
        k_panic();
    }
    k_spinlock_key_t key = k_spin_lock(&crux_red_task_lock);
    crux_red_spare_holder = k_current_get();
    k_spin_unlock(&crux_red_task_lock, key);
    return true;
}

void crux_red_spare_release(void)
{
    k_spinlock_key_t key = k_spin_lock(&crux_red_task_lock);
    crux_red_spare_holder = NULL;
    k_spin_unlock(&crux_red_task_lock, key);
    if (k_mutex_unlock(&crux_red_spare_lock) != 0) {
        k_panic();
    }
}

uint32_t RedOsTaskId(void)
{
    k_tid_t self = k_current_get();
    k_spinlock_key_t key = k_spin_lock(&crux_red_task_lock);
    uint32_t id = crux_red_task_slot_locked(self);

    if (id == 0U) {
        id = (crux_red_spare_holder == self) ? CRUX_RED_TASK_SPARE : CRUX_RED_TASK_NO_SLOT;
    }
    k_spin_unlock(&crux_red_task_lock, key);
    return id;
}
