/*
 * The file medium of the RT1060-EVKB bench image (row C3, decision D2 of
 * docs/rt1060-management-image-feasibility.md): how it is mounted, and when it may be formatted.
 *
 * The rule is one sentence: THE RAM DISK MAY BE FORMATTED AND A CARD NEVER IS. A RAM disk is memory
 * that every boot finds blank, so a failed mount is answered with a format and a second mount. A
 * card holds somebody's data, so a failed mount is the end: it is reported and nothing is written.
 *
 * bench_medium.c holds the decision, with no Zephyr in it, so that a host test can run it against
 * fake devices, including a fake card that must never be formatted. bench_medium_fs.c holds the
 * Zephyr glue, the only source of the image that names a format function.
 */
#ifndef CRUX_BENCH_MEDIUM_H
#define CRUX_BENCH_MEDIUM_H

#include <stdbool.h>
#include <stddef.h>

typedef enum {
    BENCH_MEDIUM_RAM_DISK =
        1,                /* Memory: blank at every boot, and the only kind that may be formatted */
    BENCH_MEDIUM_CARD = 2 /* A card: mounted if it mounts, and never formatted */
} bench_medium_kind_t;

typedef enum {
    BENCH_MEDIUM_MOUNTED,                   /* The medium mounted as it was */
    BENCH_MEDIUM_FORMATTED_AND_MOUNTED,     /* RAM disk only: formatted, then mounted */
    BENCH_MEDIUM_NOT_MOUNTED_NOT_FORMATTED, /* A card that did not mount: reported, nothing written
                                             */
    BENCH_MEDIUM_FORMAT_FAILED,             /* RAM disk only */
    BENCH_MEDIUM_MOUNT_FAILED_AFTER_FORMAT, /* RAM disk only: one format, one more mount, then stop
                                             */
    BENCH_MEDIUM_INVALID /* A kind that is neither, or a missing operation: no call was made */
} bench_medium_result_t;

typedef struct {
    int (*mount)(void *context);  /* 0 on success; anything else is a failure */
    int (*format)(void *context); /* Called for a RAM disk only. A card build passes NULL. */
    void *context;
} bench_medium_ops_t;

/* The whole rule. Never calls format for a kind other than BENCH_MEDIUM_RAM_DISK. */
bench_medium_result_t bench_medium_prepare(bench_medium_kind_t kind, const bench_medium_ops_t *ops);

/* The mount point of the file backend that the Kconfig form selects. bench_medium_fs.c refuses a
 * build that has neither form or both, so a source that needs the mount point has one of the two.
 */
#if defined(CONFIG_CRUX_BENCH_FILE_RAM_DISK) && !defined(CONFIG_CRUX_BENCH_FILE_MICROSD)
#define BENCH_MEDIUM_MOUNT_POINT "/RAM:"
#elif defined(CONFIG_CRUX_BENCH_FILE_MICROSD) && !defined(CONFIG_CRUX_BENCH_FILE_RAM_DISK)
#define BENCH_MEDIUM_MOUNT_POINT "/SD:"
#endif

/* Prints one BENCH line that says what was done, and returns true if the medium is mounted. */
bool bench_medium_mount(void);

#endif
