/* SPDX-License-Identifier: Apache-2.0 */
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <zephyr/drivers/flash.h>
#include <zephyr/drivers/flash/flash_simulator.h>
#include <zephyr/settings/settings.h>
#include <zephyr/storage/flash_map.h>
#include <zephyr/ztest.h>

#include "records.h"
#include <settings/settings_nvs.h>

BUILD_ASSERT(IS_ENABLED(CONFIG_BOARD_NATIVE_SIM));
BUILD_ASSERT(IS_ENABLED(CONFIG_FLASH_SIMULATOR));
BUILD_ASSERT(!IS_ENABLED(CONFIG_SETTINGS_NVS_NAME_CACHE));
BUILD_ASSERT(!IS_ENABLED(CONFIG_NVS_INIT_BAD_MEMORY_REGION));
BUILD_ASSERT(!IS_ENABLED(CONFIG_USERSPACE));
BUILD_ASSERT(DT_SAME_NODE(DT_CHOSEN(zephyr_settings_partition), DT_NODELABEL(candidate_nvs)));

#define AREA_OFFSET PARTITION_OFFSET(candidate_nvs)
#define AREA_SIZE PARTITION_SIZE(candidate_nvs)
#define KEY_BOUND 4000U
#define STATE_SIZE 4096U
static struct settings_nvs *backend;
static bool alive[STATE_SIZE], seen[STATE_SIZE];
static unsigned revisions[STATE_SIZE];
static unsigned expected_count;

static void remount(void) {
    /* No live operations; preserve registered store addresses, discard NVS/count state.
     * Name cache is off. This is a backend remount, not a process/physical reboot. */
    memset(&backend->cf_nvs, 0, sizeof(backend->cf_nvs));
    backend->cf_nvs.offset = AREA_OFFSET;
    backend->cf_nvs.sector_size = 4096;
    backend->cf_nvs.sector_count = 16;
    backend->last_name_id = 0;
    zassert_ok(settings_nvs_backend_init(backend));
    zassert_true(backend->cf_nvs.ready);
}
static void *setup(void) {
    void *storage = NULL;
    zassert_ok(settings_subsys_init());
    zassert_ok(settings_storage_get(&storage));
    zassert_not_null(storage);
    backend = CONTAINER_OF((struct nvs_fs *)storage, struct settings_nvs, cf_nvs);
    zassert_equal(backend->cf_nvs.offset, AREA_OFFSET);
    zassert_equal(backend->cf_nvs.sector_size, 4096);
    zassert_equal(backend->cf_nvs.sector_count, 16);
    zassert_equal(AREA_SIZE, 65536);
    printk("NVS_GEOMETRY offset=%u bytes=%u sectors=%u sector_size=%u crc=%d\n",
           (unsigned)AREA_OFFSET, (unsigned)AREA_SIZE, backend->cf_nvs.sector_count,
           backend->cf_nvs.sector_size, IS_ENABLED(CONFIG_NVS_DATA_CRC));
    return NULL;
}
static void before(void *fixture) {
    ARG_UNUSED(fixture);
    zassert_ok(flash_erase(backend->flash_dev, AREA_OFFSET, AREA_SIZE));
    remount();
    memset(alive, 0, sizeof(alive));
    memset(revisions, 0, sizeof(revisions));
    expected_count = 0;
}
static int save(unsigned index, unsigned revision) {
    char name[16];
    uint8_t wire[8];
    snprintk(name, sizeof(name), "cfg/p%04u", index);
    size_t length = probe_record(index, revision, wire);
    return settings_save_one(name, wire, length);
}
struct load_check {
    unsigned count;
    int error;
};
static int check_value(const char *name, size_t length, settings_read_cb read_cb, void *cb_arg,
                       void *param) {
    struct load_check *check = param;
    unsigned index;
    char tail, expected_name[16];
    uint8_t expected[8], actual[8] = {0};
    if (name == NULL || sscanf(name, "p%4u%c", &index, &tail) != 1 || index >= STATE_SIZE) {
        check->error = -EBADMSG;
        return check->error;
    }
    snprintk(expected_name, sizeof(expected_name), "p%04u", index);
    if (strcmp(name, expected_name) || !alive[index] || seen[index]) {
        check->error = -EBADMSG;
        return check->error;
    }
    size_t wanted = probe_record(index, revisions[index], expected);
    ssize_t rc = read_cb(cb_arg, actual, sizeof(actual));
    if (length != wanted || rc != (ssize_t)wanted || memcmp(actual, expected, wanted)) {
        check->error = rc < 0 ? (int)rc : -EBADMSG;
        return check->error;
    }
    seen[index] = true;
    check->count++;
    return 0;
}
static void verify_acknowledged(void) {
    struct load_check check = {0};
    memset(seen, 0, sizeof(seen));
    int rc = settings_load_subtree_direct("cfg", check_value, &check);
    /* Original Settings wrappers can hide callback errors; inspect both. */
    zassert_ok(rc);
    zassert_ok(check.error, "callback reported %d", check.error);
    zassert_equal(check.count, expected_count);
    for (unsigned i = 0; i < STATE_SIZE; ++i) {
        zassert_equal(seen[i], alive[i], "acknowledged record %u missing", i);
    }
}
ZTEST(nvs_capacity, test_mixed_roundtrip) {
    for (unsigned revision = 0; revision < 4; ++revision) {
        for (unsigned i = 0; i < 64; ++i) {
            zassert_ok(save(i, revision));
            alive[i] = true;
            revisions[i] = revision;
        }
        expected_count = 64;
        verify_acknowledged();
        remount();
        verify_acknowledged();
    }
    zassert_ok(save(1, 3), "unchanged record save");
    printk("NVS_ROUNDTRIP crc=%d keys=64 update_rounds=3 remounts=4 verified=64\n",
           IS_ENABLED(CONFIG_NVS_DATA_CRC));
}
ZTEST(nvs_capacity, test_capacity_reclamation) {
    unsigned count;
    size_t payload_bytes = 0;
    int failure = 0;
    for (count = 0; count < KEY_BOUND; ++count) {
        failure = save(count, 0);
        if (failure) {
            break;
        }
        alive[count] = true;
        expected_count++;
        payload_bytes += (count % 2) ? 8U : 5U;
        if ((count + 1) % 256 == 0) {
            printk("NVS_FILL crc=%d acknowledged=%u\n", IS_ENABLED(CONFIG_NVS_DATA_CRC), count + 1);
        }
    }
    printk("NVS_CAPACITY crc=%d raw=%u acknowledged=%u payload_bytes=%zu failed_index=%u rc=%d\n",
           IS_ENABLED(CONFIG_NVS_DATA_CRC), (unsigned)AREA_SIZE, count, payload_bytes, count,
           failure);
    zassert_true(count > 0 && count < KEY_BOUND, "saturation not observed within finite bound");
    zassert_equal(failure, -ENOSPC);
    remount();
    verify_acknowledged();
    unsigned removed = count / 2;
    for (unsigned i = 0; i < removed; ++i) {
        char name[16];
        snprintk(name, sizeof(name), "cfg/p%04u", i);
        zassert_ok(settings_delete(name));
        alive[i] = false;
        expected_count--;
    }
    /* Skip the failed new-key attempt: no rollback or ID-reuse claim. */
    for (unsigned i = count + 1; i <= count + 16; ++i) {
        zassert_ok(save(i, 0));
        alive[i] = true;
        expected_count++;
    }
    remount();
    verify_acknowledged();
    printk("NVS_RECLAIM crc=%d removed=%u new_acknowledged=16 retained=%u remount_verified=%u\n",
           IS_ENABLED(CONFIG_NVS_DATA_CRC), removed, count - removed, expected_count);
}
struct crc_read {
    unsigned calls;
    size_t advertised;
    ssize_t actual;
    uint8_t wire[8];
};
static int read_crc_value(const char *name, size_t length, settings_read_cb read_cb, void *cb_arg,
                          void *param) {
    ARG_UNUSED(name);
    struct crc_read *read = param;
    read->calls++;
    read->advertised = length;
    read->actual = read_cb(cb_arg, read->wire, sizeof(read->wire));
    return read->actual < 0 ? (int)read->actual : 0;
}
ZTEST(nvs_capacity, test_crc_full_partial_and_remount) {
    uint8_t original[8], full[8], first = 0;
    zassert_equal(probe_record(1, 0, original), sizeof(original));
    zassert_ok(settings_save_one("cfg/crc", original, sizeof(original)));
    const uint16_t id = NVS_NAMECNT_ID + 1 + NVS_NAME_ID_OFFSET;
    zassert_equal(nvs_read(&backend->cf_nvs, id, full, sizeof(full)), sizeof(original));
    zassert_mem_equal(full, original, sizeof(full));
    size_t memory_size, matches = 0, data_offset = 0;
    uint8_t *memory = flash_simulator_get_memory(backend->flash_dev, &memory_size);
    zassert_not_null(memory);
    zassert_true(memory_size >= AREA_OFFSET + AREA_SIZE);
    for (size_t i = AREA_OFFSET; i <= AREA_OFFSET + AREA_SIZE - sizeof(original); ++i) {
        if (memcmp(memory + i, original, sizeof(original)) == 0) {
            matches++;
            data_offset = i;
        }
    }
    zassert_equal(matches, 1, "payload corruption target must be unambiguous");
    memory[data_offset + 4] ^= 1; /* Explicit simulator corruption, not a Flash write failure. */
    for (unsigned phase = 0; phase < 2; ++phase) {
        if (phase) {
            remount();
        }
        ssize_t full_rc = nvs_read(&backend->cf_nvs, id, full, sizeof(full));
        ssize_t partial_rc = nvs_read(&backend->cf_nvs, id, &first, sizeof(first));
        zassert_equal(full_rc, IS_ENABLED(CONFIG_NVS_DATA_CRC) ? -EIO : 8);
        zassert_equal(partial_rc, 8, "partial read returns full advertised size without CRC check");
        zassert_equal(first, original[0]);
        if (!IS_ENABLED(CONFIG_NVS_DATA_CRC)) {
            zassert_not_equal(memcmp(full, original, sizeof(full)), 0);
        }
        struct crc_read read = {0};
        int api_rc = settings_load_subtree_direct("cfg/crc", read_crc_value, &read);
        zassert_equal(read.calls, 1);
        zassert_equal(read.advertised, 8);
        zassert_equal(read.actual, full_rc);
        if (IS_ENABLED(CONFIG_NVS_DATA_CRC)) {
            /* Pinned-baseline limitation: Settings hides the callback's -EIO.
             * This is not accepted provider behavior or permission to publish read.wire.
             */
            zassert_equal(api_rc, 0, "known baseline limitation: outer Settings hides CRC failure");
        } else {
            zassert_ok(api_rc);
        }
        printk(
            "NVS_CRC crc=%d remounted=%u full_rc=%zd partial_rc=%zd callback_rc=%zd outer_rc=%d\n",
            IS_ENABLED(CONFIG_NVS_DATA_CRC), phase, full_rc, partial_rc, read.actual, api_rc);
    }
}
ZTEST_SUITE(nvs_capacity, NULL, setup, before, NULL, NULL);
