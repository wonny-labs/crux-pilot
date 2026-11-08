/*
 * Host test of the mount and format rule of the RT1060-EVKB bench image (row C3, decision D2).
 * It runs the decision core (src/bench_medium.c) against fake devices and proves one thing from
 * several sides: a card is never formatted, whatever it reports, and only the RAM disk may be.
 * Nothing here touches a board, a card or a card reader: every device is a struct in memory.
 */
#include "bench_medium.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>

static int failures;
#define CHECK(condition, ...)                                                                      \
    do {                                                                                           \
        if (!(condition)) {                                                                        \
            failures++;                                                                            \
            printf("FAIL %s:%d: ", __FILE__, __LINE__);                                            \
            printf(__VA_ARGS__);                                                                   \
            printf("\n");                                                                          \
        }                                                                                          \
    } while (0)

/* A fake device. `blank` is what a blank card or a zeroed RAM disk looks like. */
typedef struct {
    bool blank;              /* No file system yet: mount fails until a format */
    bool format_would_help;  /* The dangerous fake: it mounts after a format */
    int mount_error;         /* The error a failing mount returns */
    int format_error;        /* 0, or the error a format returns */
    bool mounted;
    unsigned mount_calls;
    unsigned format_calls;
    unsigned char medium[16]; /* Stands for the contents: a format changes it */
} fake_t;

static int fake_mount(void *context)
{
    fake_t *fake = context;
    fake->mount_calls++;
    if (fake->blank) {
        return fake->mount_error;
    }
    fake->mounted = true;
    return 0;
}

static int fake_format(void *context)
{
    fake_t *fake = context;
    fake->format_calls++;
    if (fake->format_error != 0) {
        return fake->format_error;
    }
    memset(fake->medium, 0xF5, sizeof(fake->medium));
    if (fake->format_would_help) {
        fake->blank = false;
    }
    return 0;
}

static fake_t blank(void)
{
    return (fake_t){.blank = true, .format_would_help = true, .mount_error = -EINVAL};
}

static bench_medium_ops_t ops_of(fake_t *fake)
{
    return (bench_medium_ops_t){.mount = fake_mount, .format = fake_format, .context = fake};
}

static void ram_disk_blank_is_formatted_once_and_mounted(void)
{
    fake_t fake = blank();
    bench_medium_ops_t ops = ops_of(&fake);
    CHECK(bench_medium_prepare(BENCH_MEDIUM_RAM_DISK, &ops) == BENCH_MEDIUM_FORMATTED_AND_MOUNTED,
          "blank RAM disk");
    CHECK(fake.format_calls == 1 && fake.mount_calls == 2 && fake.mounted,
          "format %u mount %u", fake.format_calls, fake.mount_calls);
}

static void ram_disk_already_formatted_is_mounted_without_a_format(void)
{
    fake_t fake = {.blank = false};
    bench_medium_ops_t ops = ops_of(&fake);
    CHECK(bench_medium_prepare(BENCH_MEDIUM_RAM_DISK, &ops) == BENCH_MEDIUM_MOUNTED, "formatted");
    CHECK(fake.format_calls == 0 && fake.mount_calls == 1, "format %u", fake.format_calls);
}

static void ram_disk_format_failure_stops_without_a_second_mount(void)
{
    fake_t fake = blank();
    fake.format_error = -EIO;
    bench_medium_ops_t ops = ops_of(&fake);
    CHECK(bench_medium_prepare(BENCH_MEDIUM_RAM_DISK, &ops) == BENCH_MEDIUM_FORMAT_FAILED,
          "format failure");
    CHECK(fake.format_calls == 1 && fake.mount_calls == 1 && !fake.mounted, "calls");
}

static void ram_disk_that_does_not_mount_after_a_format_does_not_loop(void)
{
    fake_t fake = blank();
    fake.format_would_help = false;
    bench_medium_ops_t ops = ops_of(&fake);
    CHECK(bench_medium_prepare(BENCH_MEDIUM_RAM_DISK, &ops) ==
              BENCH_MEDIUM_MOUNT_FAILED_AFTER_FORMAT,
          "mount after format");
    CHECK(fake.format_calls == 1 && fake.mount_calls == 2, "no loop: format %u mount %u",
          fake.format_calls, fake.mount_calls);
}

/* The card: every failing mount, including the one that a format would cure, ends with no format. */
static void card_is_never_formatted_whatever_the_mount_says(void)
{
    static const int errors[] = {-EINVAL, -EIO,   -ENODEV, -ENOENT,    -EBUSY, -ENOMEM,
                                 -ENOTSUP, -ETIMEDOUT, -EFAULT, -EROFS, -1,    -100000,
                                 1,        EIO,       255};
    for (size_t i = 0; i < sizeof(errors) / sizeof(errors[0]); i++) {
        fake_t fake = blank();
        fake.mount_error = errors[i];
        bench_medium_ops_t ops = ops_of(&fake);
        unsigned char before[sizeof(fake.medium)];
        memcpy(before, fake.medium, sizeof(before));
        bench_medium_result_t result = bench_medium_prepare(BENCH_MEDIUM_CARD, &ops);
        /* Error value 1 or a positive one is not a success: only 0 is. */
        CHECK(result == BENCH_MEDIUM_NOT_MOUNTED_NOT_FORMATTED, "card, mount error %d: %d",
              errors[i], (int)result);
        CHECK(fake.format_calls == 0, "card, mount error %d: the card was formatted", errors[i]);
        CHECK(memcmp(before, fake.medium, sizeof(before)) == 0, "the card's contents changed");
        CHECK(fake.mount_calls == 1 && !fake.mounted, "card, mount error %d: calls", errors[i]);
    }
}

static void card_that_mounts_is_mounted_and_not_touched(void)
{
    fake_t fake = {.blank = false};
    bench_medium_ops_t ops = ops_of(&fake);
    CHECK(bench_medium_prepare(BENCH_MEDIUM_CARD, &ops) == BENCH_MEDIUM_MOUNTED, "card mounts");
    CHECK(fake.format_calls == 0 && fake.mount_calls == 1, "card calls");
}

static void card_without_any_format_function_is_the_same(void)
{
    fake_t fake = blank();
    bench_medium_ops_t ops = {.mount = fake_mount, .format = NULL, .context = &fake};
    CHECK(bench_medium_prepare(BENCH_MEDIUM_CARD, &ops) == BENCH_MEDIUM_NOT_MOUNTED_NOT_FORMATTED,
          "card with no format function");
    CHECK(bench_medium_prepare(BENCH_MEDIUM_RAM_DISK, &ops) == BENCH_MEDIUM_FORMAT_FAILED,
          "a RAM disk with no format function fails and does not crash");
}

/* The property: over every value of the kind that a caller could pass, a format happens only for the RAM
 * disk. A kind that is not one of the two is refused before any call at all. */
static void a_format_is_reachable_for_the_ram_disk_only(void)
{
    for (int kind = -1000; kind <= 1000; kind++) {
        fake_t fake = blank();
        bench_medium_ops_t ops = ops_of(&fake);
        bench_medium_result_t result = bench_medium_prepare((bench_medium_kind_t)kind, &ops);
        if (kind == BENCH_MEDIUM_RAM_DISK) {
            CHECK(fake.format_calls == 1, "RAM disk: format %u", fake.format_calls);
        } else {
            CHECK(fake.format_calls == 0, "kind %d reached a format", kind);
        }
        if (kind != BENCH_MEDIUM_RAM_DISK && kind != BENCH_MEDIUM_CARD) {
            CHECK(result == BENCH_MEDIUM_INVALID && fake.mount_calls == 0,
                  "kind %d is not refused before any call", kind);
        }
    }
    /* The extremes of an enum's storage type. */
    for (unsigned bits = 0; bits < 32; bits++) {
        fake_t fake = blank();
        bench_medium_ops_t ops = ops_of(&fake);
        (void)bench_medium_prepare((bench_medium_kind_t)(1u << bits), &ops);
        CHECK(bits == 0 ? fake.format_calls == 1 : fake.format_calls == 0, "kind 1<<%u", bits);
    }
    (void)bench_medium_prepare((bench_medium_kind_t)0x7fffffff, &(bench_medium_ops_t){0});
}

static void a_missing_operation_is_refused_before_any_call(void)
{
    fake_t fake = blank();
    bench_medium_ops_t no_mount = {.mount = NULL, .format = fake_format, .context = &fake};
    CHECK(bench_medium_prepare(BENCH_MEDIUM_RAM_DISK, &no_mount) == BENCH_MEDIUM_INVALID, "no mount");
    CHECK(bench_medium_prepare(BENCH_MEDIUM_RAM_DISK, NULL) == BENCH_MEDIUM_INVALID, "no ops");
    CHECK(fake.format_calls == 0, "a format without a mount");
}

int main(void)
{
    ram_disk_blank_is_formatted_once_and_mounted();
    ram_disk_already_formatted_is_mounted_without_a_format();
    ram_disk_format_failure_stops_without_a_second_mount();
    ram_disk_that_does_not_mount_after_a_format_does_not_loop();
    card_is_never_formatted_whatever_the_mount_says();
    card_that_mounts_is_mounted_and_not_touched();
    card_without_any_format_function_is_the_same();
    a_format_is_reachable_for_the_ram_disk_only();
    a_missing_operation_is_refused_before_any_call();
    printf(failures == 0 ? "bench medium: all checks passed\n" : "bench medium: %d FAILED\n",
           failures);
    return failures == 0 ? 0 : 1;
}
