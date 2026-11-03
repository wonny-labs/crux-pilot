/* SPDX-License-Identifier: Apache-2.0 */
#pragma once

#include <stdint.h>

/* Probes for the microSD physical gate: a workload that exercises the deepest FatFs paths so the
 * stack use can be read afterwards, and a removal probe that keeps writing one small file, reports
 * every result code, and after the first failure tries the documented policy (unmount, then an
 * application remount) until the volume comes back without a reboot. Project code, not a Crux API.
 * Both write to the card; use them only when the physical gate is approved for the current task. */

enum sd_probe_state {
    SD_PROBE_OK,        /* the last operation succeeded */
    SD_PROBE_LOST,      /* an operation failed; the volume was unmounted and is being remounted */
    SD_PROBE_RECOVERED, /* a remount succeeded; the next operation decides */
};

struct sd_probe {
    enum sd_probe_state state;
    uint32_t ops;        /* operations attempted */
    uint32_t failures;   /* operations with at least one failing result */
    uint32_t recoveries; /* times the state returned to OK after a loss */
    int codes[4];        /* open, write, sync, close results of the last operation */
    int remount;         /* result of the last remount attempt */
};

/* Create, write across clusters, sync, read back, rename and remove files in /SD:/settings, stat
 * and list the root. Returns 0, or the first failing result. Leaves no file behind. */
int sd_probe_workload(void);

void sd_probe_init(struct sd_probe *probe);

/* One operation (open for replace, write 32 bytes, sync, close of "probe" in the File root), or,
 * while LOST, one remount attempt. The first failure unmounts the volume. */
void sd_probe_step(struct sd_probe *probe);
