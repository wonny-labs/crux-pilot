#include "event_test.h"

#include <crux/sync.h>

#include <pthread.h>

/* Keeps the most recent lines. A suite that writes more clears them as it goes. */
#define EVENT_TEST_LINES_MAX 256u

atomic_uint event_test_checks, event_test_failures;

static pthread_mutex_t control_lock = PTHREAD_MUTEX_INITIALIZER;
static int64_t clock_ns;
static bool clock_is_real;
static crux_status_t clock_status;
static unsigned clock_reads;
static crux_realtime_snapshot_t wall;
static crux_status_t wall_status;
static unsigned wall_reads;
static event_test_hook_t wall_hook, log_hook;
static void *wall_context, *log_context;
static crux_status_t log_status;
static event_test_line_t lines[EVENT_TEST_LINES_MAX];
static size_t line_count;
static crux_status_t mutex_create_status, semaphore_create_status, thread_create_status;
static crux_thread_options_t thread_options;
static bool in_isr;

// NOLINTNEXTLINE(bugprone-reserved-identifier)
crux_status_t __real_crux_monotonic_now(crux_monotonic_time_t *out);
// NOLINTNEXTLINE(bugprone-reserved-identifier)
crux_status_t __wrap_crux_monotonic_now(crux_monotonic_time_t *out);
// NOLINTNEXTLINE(bugprone-reserved-identifier)
crux_status_t __wrap_crux_realtime_get_snapshot(crux_realtime_snapshot_t *out);
// NOLINTNEXTLINE(bugprone-reserved-identifier)
crux_status_t __wrap_crux_log_write(crux_log_level_t level, const char *tag, const char *text);
// NOLINTNEXTLINE(bugprone-reserved-identifier)
crux_status_t __real_crux_mutex_create(crux_mutex_t *out);
// NOLINTNEXTLINE(bugprone-reserved-identifier)
crux_status_t __wrap_crux_mutex_create(crux_mutex_t *out);
// NOLINTNEXTLINE(bugprone-reserved-identifier)
crux_status_t __real_crux_semaphore_create(uint32_t initial, uint32_t maximum,
                                           crux_semaphore_t *out);
// NOLINTNEXTLINE(bugprone-reserved-identifier)
crux_status_t __wrap_crux_semaphore_create(uint32_t initial, uint32_t maximum,
                                           crux_semaphore_t *out);
// NOLINTNEXTLINE(bugprone-reserved-identifier)
crux_status_t __real_crux_thread_create(const crux_thread_options_t *options,
                                        crux_thread_entry_t entry, void *argument,
                                        crux_thread_t *out);
// NOLINTNEXTLINE(bugprone-reserved-identifier)
crux_status_t __wrap_crux_thread_create(const crux_thread_options_t *options,
                                        crux_thread_entry_t entry, void *argument,
                                        crux_thread_t *out);
// NOLINTNEXTLINE(bugprone-reserved-identifier)
bool __wrap_crux_is_in_isr(void);

int event_test_finish(const char *suite)
{
    unsigned failures = atomic_load(&event_test_failures);
    printf("%s: %u checks, %u failures\n", suite, atomic_load(&event_test_checks), failures);
    return failures == 0 ? 0 : 1;
}

void event_test_reset(void)
{
    crux_security_event_test_reset();
    pthread_mutex_lock(&control_lock);
    clock_ns = INT64_C(1000) * INT64_C(1000000);
    clock_is_real = false;
    clock_status = CRUX_OK;
    clock_reads = 0;
    memset(&wall, 0, sizeof(wall));
    wall_status = CRUX_OK;
    wall_reads = 0;
    wall_hook = log_hook = NULL;
    wall_context = log_context = NULL;
    log_status = CRUX_OK;
    line_count = 0;
    mutex_create_status = semaphore_create_status = thread_create_status = CRUX_OK;
    memset(&thread_options, 0, sizeof(thread_options));
    in_isr = false;
    pthread_mutex_unlock(&control_lock);
}

void event_test_boot(void)
{
    event_test_reset();
    EVENT_CHECK(crux_security_event_init(NULL, 0) == CRUX_OK);
}

void event_test_clock_set_ms(uint64_t now_ms)
{
    pthread_mutex_lock(&control_lock);
    clock_ns = (int64_t)now_ms * INT64_C(1000000);
    pthread_mutex_unlock(&control_lock);
}

void event_test_clock_set_ns(int64_t now_ns)
{
    pthread_mutex_lock(&control_lock);
    clock_ns = now_ns;
    pthread_mutex_unlock(&control_lock);
}

void event_test_clock_advance_ms(uint64_t delta_ms)
{
    pthread_mutex_lock(&control_lock);
    clock_ns += (int64_t)delta_ms * INT64_C(1000000);
    pthread_mutex_unlock(&control_lock);
}

void event_test_clock_fail(crux_status_t status)
{
    pthread_mutex_lock(&control_lock);
    clock_status = status;
    pthread_mutex_unlock(&control_lock);
}

void event_test_clock_real(bool real)
{
    pthread_mutex_lock(&control_lock);
    clock_is_real = real;
    pthread_mutex_unlock(&control_lock);
}

unsigned event_test_clock_reads(void)
{
    pthread_mutex_lock(&control_lock);
    unsigned reads = clock_reads;
    pthread_mutex_unlock(&control_lock);
    return reads;
}

// NOLINTNEXTLINE(bugprone-reserved-identifier)
crux_status_t __wrap_crux_monotonic_now(crux_monotonic_time_t *out)
{
    pthread_mutex_lock(&control_lock);
    bool real = clock_is_real;
    crux_status_t status = clock_status;
    clock_reads += 1;
    if (!real && status == CRUX_OK) {
        out->private_ns = clock_ns;
    }
    pthread_mutex_unlock(&control_lock);
    return real ? __real_crux_monotonic_now(out) : status;
}

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters): a test control, in the snapshot's order.
void event_test_wall_set(int64_t seconds, uint32_t nanoseconds, crux_realtime_source_t source,
                         bool is_valid, bool is_synchronized)
{
    pthread_mutex_lock(&control_lock);
    wall.timestamp.seconds = seconds;
    wall.timestamp.nanoseconds = nanoseconds;
    wall.last_source = source;
    wall.is_valid = is_valid;
    wall.is_synchronized = is_synchronized;
    pthread_mutex_unlock(&control_lock);
}

void event_test_wall_fail(crux_status_t status)
{
    pthread_mutex_lock(&control_lock);
    wall_status = status;
    pthread_mutex_unlock(&control_lock);
}

unsigned event_test_wall_reads(void)
{
    pthread_mutex_lock(&control_lock);
    unsigned reads = wall_reads;
    pthread_mutex_unlock(&control_lock);
    return reads;
}

void event_test_wall_hook(event_test_hook_t hook, void *context)
{
    pthread_mutex_lock(&control_lock);
    wall_hook = hook;
    wall_context = context;
    pthread_mutex_unlock(&control_lock);
}

// NOLINTNEXTLINE(bugprone-reserved-identifier)
crux_status_t __wrap_crux_realtime_get_snapshot(crux_realtime_snapshot_t *out)
{
    pthread_mutex_lock(&control_lock);
    event_test_hook_t hook = wall_hook;
    void *context = wall_context;
    wall_reads += 1;
    pthread_mutex_unlock(&control_lock);
    if (hook != NULL) {
        hook(context);
    }
    pthread_mutex_lock(&control_lock);
    crux_status_t status = wall_status;
    if (status == CRUX_OK) {
        *out = wall;
    }
    pthread_mutex_unlock(&control_lock);
    return status;
}

void event_test_log_status(crux_status_t status)
{
    pthread_mutex_lock(&control_lock);
    log_status = status;
    pthread_mutex_unlock(&control_lock);
}

void event_test_log_hook(event_test_hook_t hook, void *context)
{
    pthread_mutex_lock(&control_lock);
    log_hook = hook;
    log_context = context;
    pthread_mutex_unlock(&control_lock);
}

// NOLINTNEXTLINE(bugprone-reserved-identifier,bugprone-easily-swappable-parameters)
crux_status_t __wrap_crux_log_write(crux_log_level_t level, const char *tag, const char *text)
{
    /* The rules the real Logging contract would reject a line for. */
    EVENT_CHECK(strcmp(tag, "crux.security") == 0);
    EVENT_CHECK(strlen(text) <= CRUX_LOG_TEXT_MAX);
    EVENT_CHECK(strchr(text, '\r') == NULL && strchr(text, '\n') == NULL);
    EVENT_CHECK(level == CRUX_LOG_INFO || level == CRUX_LOG_WARN);
    pthread_mutex_lock(&control_lock);
    event_test_hook_t hook = log_hook;
    void *context = log_context;
    pthread_mutex_unlock(&control_lock);
    if (hook != NULL) {
        hook(context);
    }
    pthread_mutex_lock(&control_lock);
    crux_status_t status = log_status;
    if (line_count == EVENT_TEST_LINES_MAX) {
        memmove(&lines[0], &lines[1], sizeof(lines) - sizeof(lines[0]));
        line_count -= 1;
    }
    lines[line_count].level = level;
    strncpy(lines[line_count].text, text, sizeof(lines[line_count].text) - 1u);
    lines[line_count].text[sizeof(lines[line_count].text) - 1u] = '\0';
    line_count += 1;
    pthread_mutex_unlock(&control_lock);
    return status;
}

size_t event_test_line_count(void)
{
    pthread_mutex_lock(&control_lock);
    size_t count = line_count;
    pthread_mutex_unlock(&control_lock);
    return count;
}

const event_test_line_t *event_test_line(size_t index)
{
    return index < event_test_line_count() ? &lines[index] : NULL;
}

void event_test_lines_clear(void)
{
    pthread_mutex_lock(&control_lock);
    line_count = 0;
    pthread_mutex_unlock(&control_lock);
}

const char *event_test_last_line(void)
{
    size_t count = event_test_line_count();
    return count == 0 ? "" : lines[count - 1u].text;
}

void event_test_mutex_create_fail(crux_status_t status)
{
    pthread_mutex_lock(&control_lock);
    mutex_create_status = status;
    pthread_mutex_unlock(&control_lock);
}

void event_test_semaphore_create_fail(crux_status_t status)
{
    pthread_mutex_lock(&control_lock);
    semaphore_create_status = status;
    pthread_mutex_unlock(&control_lock);
}

void event_test_thread_create_fail(crux_status_t status)
{
    pthread_mutex_lock(&control_lock);
    thread_create_status = status;
    pthread_mutex_unlock(&control_lock);
}

crux_thread_options_t event_test_thread_options(void)
{
    pthread_mutex_lock(&control_lock);
    crux_thread_options_t options = thread_options;
    pthread_mutex_unlock(&control_lock);
    return options;
}

// NOLINTNEXTLINE(bugprone-reserved-identifier)
crux_status_t __wrap_crux_mutex_create(crux_mutex_t *out)
{
    pthread_mutex_lock(&control_lock);
    crux_status_t status = mutex_create_status;
    mutex_create_status = CRUX_OK;
    pthread_mutex_unlock(&control_lock);
    return status != CRUX_OK ? status : __real_crux_mutex_create(out);
}

// NOLINTNEXTLINE(bugprone-reserved-identifier)
crux_status_t __wrap_crux_semaphore_create(uint32_t initial, uint32_t maximum,
                                           crux_semaphore_t *out)
{
    pthread_mutex_lock(&control_lock);
    crux_status_t status = semaphore_create_status;
    semaphore_create_status = CRUX_OK;
    pthread_mutex_unlock(&control_lock);
    return status != CRUX_OK ? status : __real_crux_semaphore_create(initial, maximum, out);
}

// NOLINTNEXTLINE(bugprone-reserved-identifier)
crux_status_t __wrap_crux_thread_create(const crux_thread_options_t *options,
                                        crux_thread_entry_t entry, void *argument,
                                        crux_thread_t *out)
{
    pthread_mutex_lock(&control_lock);
    crux_status_t status = thread_create_status;
    thread_create_status = CRUX_OK;
    thread_options = *options;
    pthread_mutex_unlock(&control_lock);
    return status != CRUX_OK ? status : __real_crux_thread_create(options, entry, argument, out);
}

void event_test_in_isr(bool value)
{
    pthread_mutex_lock(&control_lock);
    in_isr = value;
    pthread_mutex_unlock(&control_lock);
}

// NOLINTNEXTLINE(bugprone-reserved-identifier)
bool __wrap_crux_is_in_isr(void)
{
    pthread_mutex_lock(&control_lock);
    bool value = in_isr;
    pthread_mutex_unlock(&control_lock);
    return value;
}

unsigned event_test_drain_all(void)
{
    unsigned total = 0;
    unsigned lines_written;
    do {
        lines_written = crux_security_event_test_drain_step();
        total += lines_written;
    } while (lines_written != 0);
    return total;
}

void event_test_emit(crux_security_event_id_t id, crux_security_event_result_t result,
                     const char *user, const char *subject)
{
    crux_security_event_t event = {
        .id = id,
        .result = result,
        .channel = 2,
        .source_length = 4,
        .source = {192, 0, 2, 7},
        .user = user,
        .subject = subject,
    };
    crux_security_event_emit(&event);
}

void event_test_emit_routine(void)
{
    event_test_emit(CRUX_SECURITY_EVENT_ID_ACCESS_LOGIN_FAILED, CRUX_SECURITY_EVENT_DENIED, NULL,
                    NULL);
}

void event_test_emit_change(void)
{
    event_test_emit(CRUX_SECURITY_EVENT_ID_ACCESS_ACCOUNT_CREATED, CRUX_SECURITY_EVENT_OK, "admin",
                    "alice");
}

static pthread_t holder;
static crux_semaphore_t holder_acquired, holder_release;

static void *hold_ring_lock(void *context)
{
    (void)context;
    crux_mutex_t lock = crux_security_event_test_ring_lock();
    EVENT_CHECK(crux_mutex_lock(lock, 1000) == CRUX_OK);
    EVENT_CHECK(crux_semaphore_give(holder_acquired) == CRUX_OK);
    EVENT_CHECK(crux_semaphore_take(holder_release, 10000) == CRUX_OK);
    EVENT_CHECK(crux_mutex_unlock(lock) == CRUX_OK);
    return NULL;
}

void event_test_hold_ring_lock(void)
{
    EVENT_CHECK(__real_crux_semaphore_create(0, 1, &holder_acquired) == CRUX_OK);
    EVENT_CHECK(__real_crux_semaphore_create(0, 1, &holder_release) == CRUX_OK);
    EVENT_CHECK(pthread_create(&holder, NULL, hold_ring_lock, NULL) == 0);
    EVENT_CHECK(crux_semaphore_take(holder_acquired, 10000) == CRUX_OK);
}

void event_test_release_ring_lock(void)
{
    EVENT_CHECK(crux_semaphore_give(holder_release) == CRUX_OK);
    EVENT_CHECK(pthread_join(holder, NULL) == 0);
    EVENT_CHECK(crux_semaphore_destroy(&holder_acquired) == CRUX_OK);
    EVENT_CHECK(crux_semaphore_destroy(&holder_release) == CRUX_OK);
}

crux_security_event_stats_t event_test_stats(void)
{
    crux_security_event_stats_t stats;
    memset(&stats, 0xA5, sizeof(stats));
    EVENT_CHECK(crux_security_event_get_stats(&stats) == CRUX_OK);
    return stats;
}

void event_test_record_fill(crux_security_event_record_t *record)
{
    memset(record, EVENT_TEST_UNTOUCHED_BYTE, sizeof(*record));
}

bool event_test_record_untouched(const crux_security_event_record_t *record)
{
    const uint8_t *bytes = (const uint8_t *)record;
    for (size_t index = 0; index < sizeof(*record); ++index) {
        if (bytes[index] != EVENT_TEST_UNTOUCHED_BYTE) {
            return false;
        }
    }
    return true;
}

void event_test_expect_ring_lock_free(void *context)
{
    (void)context;
    crux_mutex_t lock = crux_security_event_test_ring_lock();
    crux_status_t status = crux_mutex_lock(lock, 0);
    EVENT_CHECK(status == CRUX_OK);
    if (status == CRUX_OK) {
        (void)crux_mutex_unlock(lock);
    }
}
