/* SPDX-License-Identifier: Apache-2.0 */
/*
 * Workloads and checks of the power-cut replay. Every content is a function of a tag, so a check
 * in a new process can tell which step a file comes from without any state from the run.
 *   W1  appends records to files/w1, with a sync and a mark after each
 *   W2  writes settings/w2.tmp, syncs, closes and renames it onto settings/w2, then marks
 *   W3  creates, overwrites and removes files and a directory, each change synced and marked
 * files/keep and settings/w2 generation 0 are written at provision; files/keep never again.
 */
#include "workloads.h"

#include "emmc_sim.h"
#include "fs_backend.h"

#include <errno.h>
#include <stddef.h>
#include <string.h>
#include <zephyr/fs/fs.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/sys/crc.h>
#include <zephyr/sys/util.h>

#define FILES_DIR POWERCUT_MOUNT_POINT "/files"
#define SETTINGS_DIR POWERCUT_MOUNT_POINT "/settings"
#define KEEP_PATH FILES_DIR "/keep"
#define RECOMMIT_PATH FILES_DIR "/recommit"
#define W1_PATH FILES_DIR "/w1"
#define W2_PATH SETTINGS_DIR "/w2"
#define W2_TMP_PATH SETTINGS_DIR "/w2.tmp"

#define MAX_FILE 4096U
/* fs_stat or read_file found something other than a file that fits: a stale or damaged value. */
#define WRONG_SHAPE 1

#define KEEP_TAG 0x4bU
#define KEEP_SIZE 1000U
#define RECOMMIT_TAG 0x7eU
#define RECOMMIT_SIZE 700U

#define W1_RECORDS 16U
#define W1_RECORD_SIZE 200U
#define W1_TAG(step) (0x60U + (step))

#define W2_GENERATIONS 8U
#define W2_MAGIC 0x4e473257U /* "W2GN" */
#define W2_HEADER 16U
#define W2_TAG(generation) (0x90U + (generation))

static uint8_t buffer[MAX_FILE];
static uint8_t expected[MAX_FILE];

static void fill(uint8_t *to, size_t size, uint32_t tag)
{
    for (size_t i = 0; i < size; i++) {
        to[i] = (uint8_t)(tag * 37U + i * 7U + (i >> 8));
    }
}

static void ack(uint32_t step)
{
    emmc_sim_trace_mark(step);
}

static int write_all(struct fs_file_t *file, const uint8_t *data, size_t size)
{
    ssize_t written = fs_write(file, data, size);

    if (written < 0) {
        return (int)written;
    }
    return (size_t)written == size ? 0 : -ENOSPC;
}

/*
 * Makes a file hold exactly `size` bytes of `data`, synced and closed. A step other than zero is
 * acknowledged once that returns, or before the sync with early_ack.
 */
static int replace_content(const char *path, const uint8_t *data, size_t size, uint32_t step,
                           bool early_ack)
{
    struct fs_file_t file;
    int rc;
    int closed;

    fs_file_t_init(&file);
    rc = fs_open(&file, path, FS_O_CREATE | FS_O_RDWR);
    if (rc != 0) {
        return rc;
    }
    rc = fs_truncate(&file, 0);
    if (rc == 0) {
        rc = write_all(&file, data, size);
    }
    if (rc == 0 && early_ack && step != 0U) {
        ack(step);
    }
    if (rc == 0) {
        rc = fs_sync(&file);
    }
    closed = fs_close(&file);
    if (rc == 0) {
        rc = closed;
    }
    if (rc == 0 && !early_ack && step != 0U) {
        ack(step);
    }
    return rc;
}

/* Fills `to` with generation `generation` of settings/w2 and returns its size. */
static size_t w2_content(uint8_t *to, uint32_t generation)
{
    uint32_t length = 300U + 211U * generation;

    fill(&to[W2_HEADER], length, W2_TAG(generation));
    sys_put_le32(W2_MAGIC, &to[0]);
    sys_put_le32(generation, &to[4]);
    sys_put_le32(length, &to[8]);
    sys_put_le32(crc32_ieee(&to[W2_HEADER], length), &to[12]);
    return W2_HEADER + length;
}

/* W3: five files, one of them in a directory that is created and removed. */
enum { W3_A, W3_B, W3_C, W3_D, W3_DIR, W3_E, W3_PATHS };
enum w3_op { W3_WRITE, W3_REMOVE, W3_MKDIR, W3_RMDIR };

#define W3_ABSENT 0x00U
#define W3_IS_DIR 0xffU
#define W3_STEPS 12U
#define W3_MAX 1024U

struct w3_step {
    enum w3_op op;
    uint8_t path;
    uint8_t tag; /* the content written by W3_WRITE */
};

static const char *const w3_paths[W3_PATHS] = {
    [W3_A] = FILES_DIR "/w3a", [W3_B] = FILES_DIR "/w3b",     [W3_C] = FILES_DIR "/w3c",
    [W3_D] = FILES_DIR "/w3d", [W3_DIR] = FILES_DIR "/w3dir", [W3_E] = FILES_DIR "/w3dir/e",
};

static const uint8_t w3_baseline[W3_PATHS] = {[W3_A] = 0x10};

static const struct w3_step w3_steps[W3_STEPS] = {
    {W3_WRITE, W3_A, 0x11}, {W3_WRITE, W3_B, 0x21}, {W3_MKDIR, W3_DIR, 0},  {W3_WRITE, W3_E, 0x51},
    {W3_WRITE, W3_B, 0x22}, {W3_WRITE, W3_C, 0x31}, {W3_REMOVE, W3_A, 0},   {W3_WRITE, W3_E, 0x52},
    {W3_REMOVE, W3_E, 0},   {W3_RMDIR, W3_DIR, 0},  {W3_WRITE, W3_D, 0x41}, {W3_WRITE, W3_C, 0x32},
};

static size_t w3_size(uint8_t tag)
{
    return 120U + (tag & 0x0fU) * 260U + (tag >> 4) * 16U;
}

/* The state after `steps` steps: per path, absent, a directory or the tag of its content. */
static void w3_state(uint32_t steps, uint8_t state[W3_PATHS])
{
    memcpy(state, w3_baseline, W3_PATHS);
    for (uint32_t i = 0; i < steps; i++) {
        const struct w3_step *step = &w3_steps[i];

        state[step->path] = step->op == W3_WRITE   ? step->tag
                            : step->op == W3_MKDIR ? W3_IS_DIR
                                                   : W3_ABSENT;
    }
}

static int w3_write(uint8_t path, uint8_t tag, uint32_t step, bool early_ack)
{
    size_t size = w3_size(tag);

    fill(buffer, size, tag);
    return replace_content(w3_paths[path], buffer, size, step, early_ack);
}

/*
 * The VFS has no directory sync. The file systems here make a directory operation durable before
 * it returns (FatFs syncs the volume in f_unlink and f_mkdir), so the call is its own sync.
 */
static int w3_directory_step(const struct w3_step *step, uint32_t number, bool early_ack)
{
    int rc;

    if (early_ack) {
        ack(number);
    }
    rc = step->op == W3_MKDIR ? fs_mkdir(w3_paths[step->path]) : fs_unlink(w3_paths[step->path]);
    if (rc == 0 && !early_ack) {
        ack(number);
    }
    return rc;
}

static int ensure_dir(const char *path)
{
    int rc = fs_mkdir(path);

    return rc == -EEXIST ? 0 : rc;
}

static int prepare_common(void)
{
    static bool done;
    int rc;

    if (done) {
        return 0;
    }
    rc = ensure_dir(FILES_DIR);
    if (rc == 0) {
        rc = ensure_dir(SETTINGS_DIR);
    }
    if (rc == 0) {
        fill(buffer, KEEP_SIZE, KEEP_TAG);
        rc = replace_content(KEEP_PATH, buffer, KEEP_SIZE, 0, false);
    }
    if (rc == 0) {
        rc = replace_content(W2_PATH, buffer, w2_content(buffer, 0), 0, false);
    }
    done = rc == 0;
    return rc;
}

int powercut_prepare(enum powercut_workload w)
{
    int rc = prepare_common();

    if (rc != 0) {
        return rc;
    }
    switch (w) {
    case POWERCUT_W1:
        return replace_content(W1_PATH, buffer, 0, 0, false);
    case POWERCUT_W2:
        return 0;
    case POWERCUT_W3:
        return w3_write(W3_A, w3_baseline[W3_A], 0, false);
    default:
        return -ENOTSUP; /* W4 saves through the Settings backend, which issue 7 adds */
    }
}

static int run_w1(bool early_ack)
{
    struct fs_file_t file;
    int rc;
    int closed;

    fs_file_t_init(&file);
    rc = fs_open(&file, W1_PATH, FS_O_RDWR | FS_O_APPEND);
    if (rc != 0) {
        return rc;
    }
    for (uint32_t step = 1; rc == 0 && step <= W1_RECORDS; step++) {
        fill(buffer, W1_RECORD_SIZE, W1_TAG(step));
        rc = write_all(&file, buffer, W1_RECORD_SIZE);
        if (rc == 0 && early_ack) {
            ack(step);
        }
        if (rc == 0) {
            rc = fs_sync(&file);
        }
        if (rc == 0 && !early_ack) {
            ack(step);
        }
    }
    closed = fs_close(&file);
    return rc != 0 ? rc : closed;
}

static int run_w2(bool early_ack)
{
    int rc = 0;

    for (uint32_t generation = 1; rc == 0 && generation <= W2_GENERATIONS; generation++) {
        /* With early_ack, replace_content acknowledges the step before its sync. */
        rc = replace_content(W2_TMP_PATH, buffer, w2_content(buffer, generation),
                             early_ack ? generation : 0U, early_ack);
        if (rc == 0) {
            rc = fs_rename(W2_TMP_PATH, W2_PATH);
        }
        if (rc == 0 && !early_ack) {
            ack(generation);
        }
    }
    return rc;
}

static int run_w3(bool early_ack)
{
    int rc = 0;

    for (uint32_t number = 1; rc == 0 && number <= W3_STEPS; number++) {
        const struct w3_step *step = &w3_steps[number - 1U];

        rc = step->op == W3_WRITE ? w3_write(step->path, step->tag, number, early_ack)
                                  : w3_directory_step(step, number, early_ack);
    }
    return rc;
}

int powercut_run(enum powercut_workload w, bool early_ack)
{
    switch (w) {
    case POWERCUT_W1:
        return run_w1(early_ack);
    case POWERCUT_W2:
        return run_w2(early_ack);
    case POWERCUT_W3:
        return run_w3(early_ack);
    default:
        return -ENOTSUP;
    }
}

/*
 * Reads a whole file of at most `cap` bytes. Returns 0, a negative error from the file system
 * (-ENOENT for a missing file), or WRONG_SHAPE for a directory or a larger file.
 */
static int read_file(const char *path, uint8_t *to, size_t cap, size_t *size)
{
    struct fs_dirent entry;
    struct fs_file_t file;
    ssize_t got;
    int closed;
    int rc = fs_stat(path, &entry);

    if (rc != 0) {
        return rc;
    }
    if (entry.type != FS_DIR_ENTRY_FILE || entry.size > cap) {
        return WRONG_SHAPE;
    }
    fs_file_t_init(&file);
    rc = fs_open(&file, path, FS_O_READ);
    if (rc != 0) {
        return rc;
    }
    got = fs_read(&file, to, entry.size);
    closed = fs_close(&file);
    if (got < 0) {
        return (int)got;
    }
    *size = (size_t)got;
    return closed;
}

/* A read of a file that should exist returned an error, as distinct from a stale value. */
static void note(int rc, bool should_exist, struct powercut_result *out)
{
    if (should_exist && rc < 0 && rc != -ENOENT) {
        out->read_error = true;
    }
}

static bool holds(const char *path, size_t want, struct powercut_result *out)
{
    size_t size = 0;
    int rc = read_file(path, buffer, MAX_FILE, &size);

    note(rc, true, out);
    return rc == 0 && size == want && memcmp(buffer, expected, want) == 0;
}

/* I2 for W1: records 1 to n, complete and in order, with n not below the acknowledged count. */
static bool check_w1(uint32_t acked, struct powercut_result *out)
{
    size_t size = 0;
    int rc = read_file(W1_PATH, buffer, MAX_FILE, &size);
    size_t records = size / W1_RECORD_SIZE;

    note(rc, true, out);
    if (rc != 0 || size % W1_RECORD_SIZE != 0U || records < acked || records > W1_RECORDS) {
        return false;
    }
    for (size_t i = 0; i < records; i++) {
        fill(expected, W1_RECORD_SIZE, W1_TAG(i + 1U));
        if (memcmp(&buffer[i * W1_RECORD_SIZE], expected, W1_RECORD_SIZE) != 0) {
            return false;
        }
    }
    return true;
}

/* I3: settings/w2 is one complete generation, not older than `floor`. */
static bool check_w2(uint32_t floor, struct powercut_result *out)
{
    size_t size = 0;
    int rc = read_file(W2_PATH, buffer, MAX_FILE, &size);
    uint32_t generation;

    note(rc, true, out);
    if (rc != 0 || size < W2_HEADER) {
        return false;
    }
    generation = sys_get_le32(&buffer[4]);
    if (generation < floor || generation > W2_GENERATIONS) {
        return false;
    }
    return size == w2_content(expected, generation) && memcmp(buffer, expected, size) == 0;
}

struct w3_found {
    int rc;
    bool dir;
    size_t size;
    uint8_t content[W3_MAX];
};

static struct w3_found w3_found[W3_PATHS];

static bool w3_matches(const uint8_t state[W3_PATHS])
{
    for (size_t p = 0; p < W3_PATHS; p++) {
        const struct w3_found *found = &w3_found[p];

        if (state[p] == W3_ABSENT) {
            if (found->rc != -ENOENT) {
                return false;
            }
        } else if (state[p] == W3_IS_DIR) {
            if (found->rc != 0 || !found->dir) {
                return false;
            }
        } else {
            fill(expected, w3_size(state[p]), state[p]);
            if (found->rc != 0 || found->dir || found->size != w3_size(state[p]) ||
                memcmp(found->content, expected, found->size) != 0) {
                return false;
            }
        }
    }
    return true;
}

/* I2 for W3: the files are as after some step from the acknowledged one to the last. */
static bool check_w3(uint32_t acked, struct powercut_result *out)
{
    uint8_t state[W3_PATHS];

    w3_state(MIN(acked, W3_STEPS), state);
    for (size_t p = 0; p < W3_PATHS; p++) {
        struct w3_found *found = &w3_found[p];
        struct fs_dirent entry;

        *found = (struct w3_found){0};
        found->rc = fs_stat(w3_paths[p], &entry);
        if (found->rc == 0 && entry.type == FS_DIR_ENTRY_DIR) {
            found->dir = true;
        } else if (found->rc == 0) {
            found->rc = read_file(w3_paths[p], found->content, W3_MAX, &found->size);
        }
        note(found->rc, state[p] != W3_ABSENT, out);
    }
    for (uint32_t steps = acked; steps <= W3_STEPS; steps++) {
        w3_state(steps, state);
        if (w3_matches(state)) {
            return true;
        }
    }
    return false;
}

/* I5: the volume takes a new write, sync and remount. */
static bool check_recommit(struct powercut_result *out)
{
    int rc;

    fill(expected, RECOMMIT_SIZE, RECOMMIT_TAG);
    rc = replace_content(RECOMMIT_PATH, expected, RECOMMIT_SIZE, 0, false);
    if (rc == 0) {
        rc = powercut_fs_unmount();
    }
    if (rc == 0) {
        rc = powercut_fs_mount();
    }
    return rc == 0 && holds(RECOMMIT_PATH, RECOMMIT_SIZE, out);
}

void powercut_verify(enum powercut_workload w, uint32_t acked, struct powercut_result *out)
{
    *out = (struct powercut_result){0};
    if (powercut_fs_mount() != 0) {
        return;
    }
    out->mounted = true;

    fill(expected, KEEP_SIZE, KEEP_TAG);
    out->untouched = holds(KEEP_PATH, KEEP_SIZE, out);
    out->replaced = check_w2(w == POWERCUT_W2 ? acked : 0U, out);
    switch (w) {
    case POWERCUT_W1:
        out->data = check_w1(acked, out);
        break;
    case POWERCUT_W2:
        out->data = out->replaced;
        break;
    case POWERCUT_W3:
        out->data = check_w3(acked, out);
        break;
    default:
        break;
    }
    out->recommit = check_recommit(out);
}
