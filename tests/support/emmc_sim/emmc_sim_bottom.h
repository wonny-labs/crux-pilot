/* SPDX-License-Identifier: Apache-2.0 */
/*
 * Host file access of the eMMC simulator, compiled in the native simulator runner. A host file
 * failure is not a modelled device behaviour, so it ends the process with an error.
 */
#ifndef CRUX_TESTS_EMMC_SIM_BOTTOM_H
#define CRUX_TESTS_EMMC_SIM_BOTTOM_H

#include <stdint.h>

/* Fills `data` from the image, or creates the image from `data` if there is no file at `path`.
 * An existing image of any other size is an error. */
void emmc_sim_bottom_load_image(const char *path, void *data, uint32_t size);
/* Does nothing when no image was loaded. */
void emmc_sim_bottom_store_image(uint32_t offset, const void *data, uint32_t size);
void emmc_sim_bottom_open_trace(const char *path);
/* Does nothing when no trace was opened. */
void emmc_sim_bottom_append_trace(const void *data, uint32_t size);

#endif /* CRUX_TESTS_EMMC_SIM_BOTTOM_H */
