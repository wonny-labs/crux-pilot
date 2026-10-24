/* SPDX-License-Identifier: Apache-2.0 */
/* Private exclusive simulator composition; caller serializes finish and calls.
 * Wrap the private File port, retaining portable validation/zero-I/O precedence.
 * No automatic reset, retry, format, repair, or independent native Flash clients. */
#include "maintenance.h"
#include "file_port.h"
#include "gate.h"
#include <errno.h>
#include <string.h>
#include <zephyr/storage/flash_map.h>
static bool handles[CRUX_FILE_POOL_SIZE];
static bool ended, uncertain, provisioning;
unsigned maintenance_file_dispatches;
void maintenance_newboot(void) {
    /* Test-only boot boundary: all public/native handles have been retired. */
    memset(handles, 0, sizeof(handles));
    ended = uncertain = provisioning = false;
    maintenance_file_dispatches = 0;
}
bool maintenance_finish(void) {
    for (size_t s = 0; s < CRUX_FILE_POOL_SIZE; ++s)
        if (handles[s])
            return false;
    if (!ended) {
        ended = true;
        if (!gate_end_maintenance())
            uncertain = true;
    }
    return !uncertain;
}
static bool allowed(void) {
    return !ended && !uncertain && gate_is_ready();
}
static void storage_uncertain(void) {
    provisioning = false;
    uncertain = true;
    (void)gate_end_maintenance();
}
/* Native IO includes unknown backend errors and possible corruption/partial
 * effects. Do not infer rollback. Ordinary NOT_FOUND, NO_SPACE or PARTIAL are
 * left to the existing contract; failed sync/close acknowledgements below are
 * independently uncertain even when their result is NO_SPACE. */
static crux_file_result_t observed(crux_file_result_t rc) {
    if (rc == CRUX_FILE_ERR_IO)
        storage_uncertain();
    return rc;
}
extern crux_file_result_t __real_crux_file_port_open(size_t, const char *, crux_file_mode_t);
crux_file_result_t __wrap_crux_file_port_open(size_t s, const char *n, crux_file_mode_t m) {
    if (!allowed())
        return CRUX_FILE_ERR_NOT_READY;
    ++maintenance_file_dispatches;
    crux_file_result_t rc = observed(__real_crux_file_port_open(s, n, m));
    if (rc == CRUX_FILE_OK)
        handles[s] = true;
    return rc;
}
extern crux_file_result_t __real_crux_file_port_close(size_t);
crux_file_result_t __wrap_crux_file_port_close(size_t s) {
    if (ended)
        return CRUX_FILE_ERR_NOT_READY;
    /* Cleanup is still allowed after a sync/close fault. Native close consumes
     * its handle even on error; an error never proves safe persistence/rollback. */
    ++maintenance_file_dispatches;
    crux_file_result_t rc = __real_crux_file_port_close(s);
    handles[s] = false;
    if (rc != CRUX_FILE_OK)
        storage_uncertain();
    return rc;
}
extern crux_file_result_t __real_crux_file_port_sync(size_t);
crux_file_result_t __wrap_crux_file_port_sync(size_t s) {
    if (!allowed())
        return CRUX_FILE_ERR_NOT_READY;
    ++maintenance_file_dispatches;
    crux_file_result_t rc = __real_crux_file_port_sync(s);
    if (rc != CRUX_FILE_OK)
        storage_uncertain();
    return rc;
}
extern crux_file_result_t __real_crux_file_port_truncate(size_t);
crux_file_result_t __wrap_crux_file_port_truncate(size_t s) {
    if (!allowed())
        return CRUX_FILE_ERR_NOT_READY;
    ++maintenance_file_dispatches;
    return observed(__real_crux_file_port_truncate(s));
}
extern crux_file_result_t __real_crux_file_port_read(size_t, void *, size_t, size_t *);
crux_file_result_t __wrap_crux_file_port_read(size_t s, void *b, size_t l, size_t *n) {
    if (!allowed())
        return CRUX_FILE_ERR_NOT_READY;
    ++maintenance_file_dispatches;
    return observed(__real_crux_file_port_read(s, b, l, n));
}
extern crux_file_result_t __real_crux_file_port_write(size_t, const void *, size_t, size_t *);
crux_file_result_t __wrap_crux_file_port_write(size_t s, const void *b, size_t l, size_t *n) {
    if (!allowed())
        return CRUX_FILE_ERR_NOT_READY;
    ++maintenance_file_dispatches;
    return observed(__real_crux_file_port_write(s, b, l, n));
}
extern crux_file_result_t __real_crux_file_port_seek(size_t, uint64_t);
crux_file_result_t __wrap_crux_file_port_seek(size_t s, uint64_t o) {
    if (!allowed())
        return CRUX_FILE_ERR_NOT_READY;
    ++maintenance_file_dispatches;
    return observed(__real_crux_file_port_seek(s, o));
}
extern crux_file_result_t __real_crux_file_port_remove(const char *);
crux_file_result_t __wrap_crux_file_port_remove(const char *n) {
    if (!allowed())
        return CRUX_FILE_ERR_NOT_READY;
    ++maintenance_file_dispatches;
    return observed(__real_crux_file_port_remove(n));
}

/* LittleFS can flush/commit during consuming close. Fence the actual selected
 * File area's mutation callbacks, including flatten (the real erase path), so
 * cleanup can release native resources without authorizing a media drain. */
unsigned maintenance_media_writes, maintenance_media_erases, maintenance_media_denied;
void maintenance_provisioning(bool enabled) {
    provisioning = enabled;
}
static bool permit_file_media(const struct flash_area *fa) {
    if (fa->fa_id != PARTITION_ID(file_test_partition) || provisioning || allowed())
        return true;
    ++maintenance_media_denied;
    return false;
}
extern int __real_flash_area_write(const struct flash_area *, off_t, const void *, size_t);
int __wrap_flash_area_write(const struct flash_area *fa, off_t off, const void *bytes, size_t len) {
    if (!permit_file_media(fa))
        return -EACCES;
    if (fa->fa_id == PARTITION_ID(file_test_partition))
        ++maintenance_media_writes;
    int rc = __real_flash_area_write(fa, off, bytes, len);
    if (rc && fa->fa_id == PARTITION_ID(file_test_partition))
        storage_uncertain();
    return rc;
}
extern int __real_flash_area_erase(const struct flash_area *, off_t, size_t);
int __wrap_flash_area_erase(const struct flash_area *fa, off_t off, size_t len) {
    if (!permit_file_media(fa))
        return -EACCES;
    if (fa->fa_id == PARTITION_ID(file_test_partition))
        ++maintenance_media_erases;
    int rc = __real_flash_area_erase(fa, off, len);
    if (rc && fa->fa_id == PARTITION_ID(file_test_partition))
        storage_uncertain();
    return rc;
}
extern int __real_flash_area_flatten(const struct flash_area *, off_t, size_t);
int __wrap_flash_area_flatten(const struct flash_area *fa, off_t off, size_t len) {
    if (!permit_file_media(fa))
        return -EACCES;
    if (fa->fa_id == PARTITION_ID(file_test_partition))
        ++maintenance_media_erases;
    int rc = __real_flash_area_flatten(fa, off, len);
    if (rc && fa->fa_id == PARTITION_ID(file_test_partition))
        storage_uncertain();
    return rc;
}
