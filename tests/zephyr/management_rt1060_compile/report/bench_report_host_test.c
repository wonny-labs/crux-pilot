/*
 * Host test of the pure parts of the bench report: the line formatter (bench_report_format.c) and
 * the activity policy (bench_report_policy.c). It builds with any C compiler, with no Zephyr, and
 * prints a sample report on stdout with --sample so that the Python checker reads what the C code
 * wrote. tests/build/test_management_bench_report.py compiles and runs it.
 */
#include "bench_report_format.h"
#include "bench_report_policy.h"

#include <stdio.h>
#include <string.h>

static int failures;

#define CHECK(condition)                                                                           \
    do {                                                                                           \
        if (!(condition)) {                                                                        \
            printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #condition);                             \
            failures++;                                                                            \
        }                                                                                          \
    } while (0)

#define CHECK_LINE(call, expected)                                                                 \
    do {                                                                                           \
        size_t length = (call);                                                                    \
        CHECK(length == strlen(expected));                                                         \
        CHECK(strcmp(buffer, expected) == 0);                                                      \
    } while (0)

static const char fingerprint[] = "00:11:22:33:44:55:66:77:88:99:AA:BB:CC:DD:EE:FF:"
                                  "00:11:22:33:44:55:66:77:88:99:AA:BB:CC:DD:EE:FF";

static void test_lines(void)
{
    char buffer[BENCH_REPORT_LINE_MAX];

    CHECK_LINE(
        bench_report_format_begin(buffer, sizeof(buffer), 3u, BENCH_REPORT_QUIET, 123456u, 2u),
        "BENCH-REPORT v=1 begin seq=3 reason=quiet uptime_ms=123456 bursts=2");
    CHECK_LINE(
        bench_report_format_stack(buffer, sizeof(buffer), 3u, "http_server_tid", 8192u, 5088u,
                                  false),
        "BENCH-STACK seq=3 thread=http_server_tid size=8192 used=3104 unused=5088 state=live");
    CHECK_LINE(bench_report_format_stack(buffer, sizeof(buffer), 3u, "main", 16384u, 100u, true),
               "BENCH-STACK seq=3 thread=main size=16384 used=16284 unused=100 state=ended");
    CHECK_LINE(
        bench_report_format_heap(buffer, sizeof(buffer), 3u, 524288u, 90000u, 100448u, 400000u),
        "BENCH-HEAP seq=3 arena=524288 in_use=90000 peak=100448 free=400000");
    CHECK_LINE(bench_report_format_end(buffer, sizeof(buffer), 3u, 17u, 0u),
               "BENCH-REPORT v=1 end seq=3 lines=17 missed=0");
    CHECK_LINE(bench_report_format_cert(buffer, sizeof(buffer), fingerprint, true),
               "BENCH-CERT v=1 sha256=00:11:22:33:44:55:66:77:88:99:AA:BB:CC:DD:EE:FF:"
               "00:11:22:33:44:55:66:77:88:99:AA:BB:CC:DD:EE:FF source=generated");
    /* The largest values the fields take still fit. */
    CHECK_LINE(bench_report_format_begin(buffer, sizeof(buffer), 4294967295u, BENCH_REPORT_IDLE,
                                         4294967295u, 4294967295u),
               "BENCH-REPORT v=1 begin seq=4294967295 reason=idle uptime_ms=4294967295 "
               "bursts=4294967295");
    CHECK_LINE(bench_report_format_stack(buffer, sizeof(buffer), 0u, "main", 0u, 0u, false),
               "BENCH-STACK seq=0 thread=main size=0 used=0 unused=0 state=live");
}

/* The worst case line is the stack line with the longest name and the largest numbers. */
static void test_longest_line_fits(void)
{
    char buffer[BENCH_REPORT_LINE_MAX];
    char name[BENCH_REPORT_NAME_MAX + 1];
    size_t length;

    memset(name, 'n', BENCH_REPORT_NAME_MAX - 1u);
    name[BENCH_REPORT_NAME_MAX - 1u] = '\0';
    length =
        bench_report_format_stack(buffer, sizeof(buffer), 4294967295u, name, 4294967295u, 0u, true);
    CHECK(length != 0u);
    CHECK(length < BENCH_REPORT_LINE_MAX);
    CHECK(strlen(buffer) == length);
}

static void test_bounds(void)
{
    char buffer[BENCH_REPORT_LINE_MAX];
    char small[16];
    char guarded[BENCH_REPORT_LINE_MAX + 8];

    /* A buffer that is too small gives 0 and an empty string, never a cut line. */
    memset(small, 'x', sizeof(small));
    CHECK(bench_report_format_heap(small, sizeof(small), 1u, 1u, 1u, 1u, 1u) == 0u);
    CHECK(small[0] == '\0');
    CHECK(bench_report_format_heap(NULL, 10u, 1u, 1u, 1u, 1u, 1u) == 0u);
    CHECK(bench_report_format_heap(buffer, 0u, 1u, 1u, 1u, 1u, 1u) == 0u);
    /* Exactly the length plus the terminator fits, one less does not. */
    {
        const char *expected = "BENCH-HEAP seq=1 arena=1 in_use=1 peak=1 free=1";
        size_t need = strlen(expected) + 1u;
        memset(guarded, 0x5a, sizeof(guarded));
        CHECK(bench_report_format_heap(guarded, need, 1u, 1u, 1u, 1u, 1u) == need - 1u);
        CHECK(strcmp(guarded, expected) == 0);
        CHECK((unsigned char)guarded[need] == 0x5au); /* nothing written past the capacity */
        CHECK(bench_report_format_heap(guarded, need - 1u, 1u, 1u, 1u, 1u, 1u) == 0u);
    }
    /* A line is never longer than the limit, whatever the numbers. */
    CHECK(bench_report_format_stack(buffer, sizeof(buffer), 4294967295u, "http_server_tid",
                                    4294967295u, 4294967295u, true) < BENCH_REPORT_LINE_MAX);
    /* A row that does not add up is refused: unused above size. */
    CHECK(bench_report_format_stack(buffer, sizeof(buffer), 1u, "main", 100u, 101u, false) == 0u);
    CHECK(buffer[0] == '\0');
    CHECK(bench_report_format_stack(buffer, sizeof(buffer), 1u, NULL, 100u, 1u, false) == 0u);
    CHECK(bench_report_format_stack(buffer, sizeof(buffer), 1u, "", 100u, 1u, false) == 0u);
    CHECK(bench_report_format_begin(buffer, sizeof(buffer), 1u, (bench_report_reason_t)99, 1u,
                                    1u) == 0u);
}

static void test_certificate_text(void)
{
    char buffer[BENCH_REPORT_LINE_MAX];
    char bad[sizeof(fingerprint)];

    CHECK(bench_report_format_cert(buffer, sizeof(buffer), NULL, true) == 0u);
    CHECK(bench_report_format_cert(buffer, sizeof(buffer), "", true) == 0u);
    CHECK(bench_report_format_cert(buffer, sizeof(buffer), "AA:BB", true) == 0u);
    /* Lowercase is not the module's form, and openssl prints uppercase: refuse it. */
    strcpy(bad, fingerprint);
    bad[3] = 'a';
    CHECK(bench_report_format_cert(buffer, sizeof(buffer), bad, true) == 0u);
    strcpy(bad, fingerprint);
    bad[2] = '-';
    CHECK(bench_report_format_cert(buffer, sizeof(buffer), bad, true) == 0u);
    strcpy(bad, fingerprint);
    bad[0] = 'G';
    CHECK(bench_report_format_cert(buffer, sizeof(buffer), bad, true) == 0u);
    /* Too long. */
    {
        char longer[sizeof(fingerprint) + 3];
        strcpy(longer, fingerprint);
        strcat(longer, ":00");
        CHECK(bench_report_format_cert(buffer, sizeof(buffer), longer, true) == 0u);
    }
    CHECK(strstr((bench_report_format_cert(buffer, sizeof(buffer), fingerprint, false), buffer),
                 "source=stored") != NULL);
}

static void test_names(void)
{
    char name[BENCH_REPORT_NAME_MAX];

    CHECK(bench_report_sanitize_name(name, sizeof(name), "rx_q[0]", 0x1234u) == 7u);
    CHECK(strcmp(name, "rx_q[0]") == 0);
    CHECK(bench_report_sanitize_name(name, sizeof(name), "bad name=x\n", 1u) == 11u);
    CHECK(strcmp(name, "bad_name_x_") == 0);
    CHECK(bench_report_sanitize_name(name, sizeof(name), NULL, 0x80012abcu) > 0u);
    CHECK(strcmp(name, "thread@80012abc") == 0);
    CHECK(bench_report_sanitize_name(name, sizeof(name), "", 0xffu) > 0u);
    CHECK(strcmp(name, "thread@ff") == 0);
    CHECK(bench_report_sanitize_name(name, sizeof(name),
                                     "a_name_that_is_far_too_long_for_the_limit",
                                     0u) == BENCH_REPORT_NAME_MAX - 1u);
    CHECK(strlen(name) == BENCH_REPORT_NAME_MAX - 1u);
    CHECK(bench_report_sanitize_name(name, 0u, "x", 0u) == 0u);
    CHECK(bench_report_sanitize_name(NULL, 8u, "x", 0u) == 0u);
    /* Only the characters the checker accepts come out, whatever goes in. */
    {
        char raw[8] = {'a', (char)0xff, '\t', ' ', '"', ';', '|', '\0'};
        CHECK(bench_report_sanitize_name(name, sizeof(name), raw, 0u) == 7u);
        for (size_t i = 0; name[i] != '\0'; i++) {
            char c = name[i];
            CHECK((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
                  c == '_' || c == '.' || c == '[' || c == ']' || c == '-' || c == '@');
        }
    }
}

typedef struct {
    unsigned boot, quiet, idle;
    uint32_t first_quiet_ms;
} trace_t;

/* Runs the policy over a trace: segments(t) is the counter at the time. */
static trace_t run(bench_report_policy_t *policy, uint32_t from_ms, uint32_t to_ms,
                   uint32_t (*segments)(uint32_t), uint32_t *last_snapshot_ms)
{
    trace_t trace = {0u, 0u, 0u, 0u};
    for (uint32_t t = from_ms; t != to_ms; t += BENCH_REPORT_POLL_MS) {
        bench_report_action_t action = bench_report_policy_step(policy, t, segments(t));
        if (action == BENCH_REPORT_ACTION_NONE) {
            continue;
        }
        if (last_snapshot_ms != NULL) {
            if (trace.boot + trace.quiet + trace.idle > 0u) {
                CHECK(t - *last_snapshot_ms >= BENCH_REPORT_MIN_GAP_MS);
            }
            *last_snapshot_ms = t;
        }
        if (action == BENCH_REPORT_ACTION_BOOT) {
            trace.boot++;
        } else if (action == BENCH_REPORT_ACTION_QUIET) {
            trace.quiet++;
            if (trace.first_quiet_ms == 0u) {
                trace.first_quiet_ms = t;
            }
        } else if (action == BENCH_REPORT_ACTION_IDLE) {
            trace.idle++;
        }
    }
    return trace;
}

static uint32_t flat(uint32_t t)
{
    (void)t;
    return 7u;
}

/* Traffic between 10 s and 12 s: a client. */
static uint32_t one_client(uint32_t t)
{
    return t < 10000u ? 0u : (t < 12000u ? (t - 10000u) / 50u : 40u);
}

static uint32_t chatter(uint32_t t)
{
    return t / 1500u; /* a segment every 1.5 s, for ever: never quiet for the quiet period */
}

static uint32_t pulses(uint32_t t)
{
    return t / 2500u; /* a segment every 2.5 s: quiet for the period, then active again */
}

static void test_policy(void)
{
    bench_report_policy_t policy;
    trace_t trace;
    uint32_t last = 0u;

    /* Boot once, idle once, nothing more on a quiet link. */
    bench_report_policy_init(&policy, 0u, 7u);
    trace = run(&policy, 0u, 600000u, flat, &last);
    CHECK(trace.boot == 1u && trace.quiet == 0u && trace.idle == 1u);

    /* One client: one quiet snapshot after its traffic stops, then one idle. */
    bench_report_policy_init(&policy, 0u, 0u);
    trace = run(&policy, 0u, 600000u, one_client, &last);
    CHECK(trace.boot == 1u && trace.quiet == 1u && trace.idle == 1u);
    CHECK(trace.first_quiet_ms >= 12000u + BENCH_REPORT_QUIET_MS - BENCH_REPORT_POLL_MS);
    CHECK(trace.first_quiet_ms <= 12000u + BENCH_REPORT_QUIET_MS + 2u * BENCH_REPORT_POLL_MS);

    /* Constant chatter is one long burst: only the boot snapshot, and never an idle one. */
    bench_report_policy_init(&policy, 0u, 0u);
    trace = run(&policy, 0u, 600000u, chatter, &last);
    CHECK(trace.boot == 1u && trace.quiet == 0u && trace.idle == 0u);

    /* The fastest pattern that goes quiet each time: the snapshots keep the minimum gap, which
     * run() checks, and ten minutes hold at most 600 s / 5 s of them. Printing is bounded. */
    bench_report_policy_init(&policy, 0u, 0u);
    trace = run(&policy, 0u, 600000u, pulses, &last);
    CHECK(trace.boot + trace.quiet + trace.idle <= 600u / (BENCH_REPORT_MIN_GAP_MS / 1000u) + 1u);
    CHECK(trace.quiet >= 1u);

    /* The clock and the counter wrap: a client across the wrap of 32-bit milliseconds is seen. */
    bench_report_policy_init(&policy, 0xffffff00u, 5u);
    {
        unsigned quiet = 0u, boot = 0u;
        uint32_t t = 0xffffff00u;
        for (int i = 0; i < 400; i++, t += BENCH_REPORT_POLL_MS) {
            uint32_t counter = (i >= 20 && i < 30) ? 0xfffffff0u + (uint32_t)i : 5u;
            bench_report_action_t action = bench_report_policy_step(&policy, t, counter);
            boot += action == BENCH_REPORT_ACTION_BOOT;
            quiet += action == BENCH_REPORT_ACTION_QUIET;
        }
        CHECK(boot == 1u && quiet == 1u);
    }
}

static void print_sample(void)
{
    char line[BENCH_REPORT_LINE_MAX];
    static const struct {
        const char *name;
        uint32_t size, unused;
        bool ended;
    } rows[] = {{"main", 16384u, 2000u, true},
                {"idle", 320u, 200u, false},
                {"http_server_tid", 8192u, 4608u, false},
                {"ssh_listener", 4096u, 2900u, false},
                {"ssh_worker", 16384u, 6000u, false},
                {"ssh_worker", 16384u, 5200u, true},
                {"bench_report", 2048u, 1500u, false}};
    uint32_t lines = 0u;

    if (bench_report_format_cert(line, sizeof(line), fingerprint, true) != 0u) {
        puts(line);
    }
    for (unsigned seq = 1u; seq <= 2u; seq++) {
        lines = 0u;
        bench_report_format_begin(line, sizeof(line), seq,
                                  seq == 1u ? BENCH_REPORT_BOOT : BENCH_REPORT_QUIET, 1000u * seq,
                                  seq - 1u);
        puts(line);
        for (size_t i = 0; i < sizeof(rows) / sizeof(rows[0]); i++) {
            bench_report_format_stack(line, sizeof(line), seq, rows[i].name, rows[i].size,
                                      rows[i].unused - 10u * seq, rows[i].ended);
            puts(line);
            lines++;
        }
        bench_report_format_heap(line, sizeof(line), seq, 524288u, 90000u * seq, 100448u * seq,
                                 300000u);
        puts(line);
        lines++;
        bench_report_format_end(line, sizeof(line), seq, lines, 0u);
        puts(line);
    }
}

int main(int argc, char **argv)
{
    if (argc > 1 && strcmp(argv[1], "--sample") == 0) {
        print_sample();
        return 0;
    }
    test_lines();
    test_longest_line_fits();
    test_bounds();
    test_certificate_text();
    test_names();
    test_policy();
    if (failures != 0) {
        printf("%d checks failed\n", failures);
        return 1;
    }
    puts("bench report host test: all checks passed");
    return 0;
}
