/* The polled lock, decision C4: only a save waits on storage, and nothing but another save waits
 * on a save. A thread that waited here would hang the case until CTest's timeout. */
#include "service_test.h"

#include <pthread.h>
#include <stdatomic.h>
#include <time.h>
#include <unistd.h>

typedef struct {
    crux_config_service_result_t next, read, update, try_read, try_update, second_save;
    size_t cursor_after_next;
} seen_during_save_t;

static void *while_a_save_runs(void *argument)
{
    seen_during_save_t *seen = argument;
    size_t cursor = 0;
    crux_config_service_item_t item;
    seen->next = crux_config_service_next(s_both, &cursor, &item);
    seen->cursor_after_next = cursor;
    char text[CRUX_CONFIG_SERVICE_TEXT_SIZE];
    size_t length;
    crux_config_type_t type;
    seen->read = crux_config_service_read(s_both, NAME_LIMIT, text, sizeof(text), &length, &type);
    seen->update = update_text(s_both, NAME_LIMIT, "8");
    crux_config_value_t value = {.type = CRUX_CONFIG_U32, .data.u32 = 9};
    seen->try_update = crux_config_service_try_update(NAME_LIMIT, &value);
    seen->try_read = crux_config_service_try_read(NAME_LIMIT, &value, NULL, 0);
    crux_config_service_save_report_t report;
    /* This build lowers the save's wait, so that this returns soon. */
    seen->second_save = crux_config_service_save(s_saver, &report);
    return NULL;
}

/* Runs inside the provider's write, with the service lock held across storage. */
static void hook_during_save(void *context)
{
    pthread_t thread;
    CHECK(pthread_create(&thread, NULL, while_a_save_runs, context) == 0);
    CHECK(pthread_join(thread, NULL) == 0);
    config_fault_write_hook(NULL, NULL);
}

static void busy_during_a_held_save(void)
{
    fixture_start();
    CHECK(update_text(s_both, NAME_MODE, "true") == CRUX_CONFIG_SERVICE_OK);
    config_events_clear();
    seen_during_save_t seen = {0};
    config_fault_write_hook(hook_during_save, &seen);

    crux_config_service_save_report_t report;
    CHECK(crux_config_service_save(s_saver, &report) == CRUX_CONFIG_SERVICE_OK);
    CHECK(seen.next == CRUX_CONFIG_SERVICE_ERR_BUSY);
    CHECK(seen.cursor_after_next == 0);
    CHECK(seen.read == CRUX_CONFIG_SERVICE_ERR_BUSY);
    CHECK(seen.update == CRUX_CONFIG_SERVICE_ERR_BUSY);
    CHECK(seen.try_read == CRUX_CONFIG_SERVICE_ERR_BUSY);
    CHECK(seen.try_update == CRUX_CONFIG_SERVICE_ERR_BUSY);
    CHECK(seen.second_save == CRUX_CONFIG_SERVICE_ERR_BUSY);

    /* A polled update that lost changed nothing, and left no event. */
    expect_u32(NAME_LIMIT, 5);
    CHECK(report.saved == 1);
    CHECK(config_events_of(CRUX_CONFIG_SERVICE_EVENT_UPDATED) == 0);
    CHECK(config_events_of(CRUX_CONFIG_SERVICE_EVENT_REJECTED) == 0);

    /* Afterwards everything works again. */
    CHECK(update_text(s_both, NAME_LIMIT, "8") == CRUX_CONFIG_SERVICE_OK);
    expect_u32(NAME_LIMIT, 8);
}

/* A validator that calls back fails its call, and neither deadlocks nor spoils the operation. */
static crux_config_service_result_t from_validator;
static crux_config_service_result_t save_from_validator;

static bool reentrant_validator(const crux_config_value_t *value, void *context)
{
    (void)value;
    (void)context;
    crux_config_value_t out;
    from_validator = crux_config_service_try_read("reentrant", &out, NULL, 0);
    crux_config_service_save_report_t report;
    save_from_validator = crux_config_service_save_local(&report);
    return true;
}

static void validator_relock(void)
{
    fixture_boot();
    static const crux_config_spec_t spec = {
        "reentrant", 1, {.type = CRUX_CONFIG_U32, .data.u32 = 1}, reentrant_validator, NULL};
    static crux_config_param_t parameter;
    CHECK(crux_config_init(&parameter, &spec) == CRUX_CONFIG_OK);
    static const crux_config_service_entry_t table[] = {
        {&parameter, "cfg.read", "cfg.write", false, CRUX_CONFIG_ACTIVATION_APPLICATION}};
    CHECK(crux_config_service_start(table, 1) == CRUX_CONFIG_SERVICE_OK);
    CHECK(update_text(s_both, "reentrant", "2") == CRUX_CONFIG_SERVICE_OK);
    CHECK(from_validator == CRUX_CONFIG_SERVICE_ERR_NOT_READY);
    CHECK(save_from_validator == CRUX_CONFIG_SERVICE_ERR_NOT_READY);
    expect_u32("reentrant", 2);
}

/* Each thread does its own work, until a shared stop. Under sanitizers this shows what the lock
 * protects: a string reads whole, the marks stay coherent, and nothing fails unexpectedly. */
static atomic_bool stop;
static atomic_uint unexpected;
static crux_access_session_t session_of_reader, session_of_writer, session_of_saver;

static const char *const texts[] = {"aaaaaaaa", "bbbbbbbbbbbbbbbbbbbbbb", "c"};

static bool valid_text(const char *text)
{
    for (size_t i = 0; i < sizeof(texts) / sizeof(texts[0]); ++i) {
        if (strcmp(text, texts[i]) == 0) {
            return true;
        }
    }
    return strcmp(text, "crux") == 0;
}

static void *reader_thread(void *argument)
{
    (void)argument;
    while (!atomic_load(&stop)) {
        char text[CRUX_CONFIG_SERVICE_TEXT_SIZE];
        size_t length;
        crux_config_type_t type;
        crux_config_service_result_t result = crux_config_service_read(
            session_of_reader, NAME_HOST, text, sizeof(text), &length, &type);
        if (result == CRUX_CONFIG_SERVICE_OK) {
            if (!valid_text(text) || strlen(text) != length) {
                atomic_fetch_add(&unexpected, 1u);
            }
        } else if (result != CRUX_CONFIG_SERVICE_ERR_BUSY) {
            atomic_fetch_add(&unexpected, 1u);
        }
        size_t cursor = 0;
        crux_config_service_item_t item;
        result = crux_config_service_next(session_of_reader, &cursor, &item);
        if (result != CRUX_CONFIG_SERVICE_OK && result != CRUX_CONFIG_SERVICE_ERR_BUSY) {
            atomic_fetch_add(&unexpected, 1u);
        }
    }
    return NULL;
}

static void *writer_thread(void *argument)
{
    (void)argument;
    for (unsigned i = 0; !atomic_load(&stop); ++i) {
        crux_config_service_item_t applied;
        const char *text = texts[i % 3];
        crux_config_service_result_t result =
            crux_config_service_update(session_of_writer, NAME_HOST, text, strlen(text), &applied);
        if (result != CRUX_CONFIG_SERVICE_OK && result != CRUX_CONFIG_SERVICE_ERR_BUSY) {
            atomic_fetch_add(&unexpected, 1u);
        }
    }
    return NULL;
}

static void *control_thread(void *argument)
{
    (void)argument;
    /* Keeps its last snapshot on BUSY, as a control thread would. */
    char snapshot[CRUX_CONFIG_SERVICE_TEXT_SIZE] = "crux";
    for (unsigned i = 0; !atomic_load(&stop); ++i) {
        char text[CRUX_CONFIG_SERVICE_TEXT_SIZE];
        crux_config_value_t value;
        crux_config_service_result_t result =
            crux_config_service_try_read(NAME_HOST, &value, text, sizeof(text));
        if (result == CRUX_CONFIG_SERVICE_OK) {
            snprintf(snapshot, sizeof(snapshot), "%s", text);
        } else if (result != CRUX_CONFIG_SERVICE_ERR_BUSY) {
            atomic_fetch_add(&unexpected, 1u);
        }
        if (!valid_text(snapshot)) {
            atomic_fetch_add(&unexpected, 1u);
        }
        crux_config_value_t candidate = {.type = CRUX_CONFIG_U32, .data.u32 = 1u + i % 100u};
        result = crux_config_service_try_update(NAME_LIMIT, &candidate);
        if (result != CRUX_CONFIG_SERVICE_OK && result != CRUX_CONFIG_SERVICE_ERR_BUSY) {
            atomic_fetch_add(&unexpected, 1u);
        }
    }
    return NULL;
}

static void *saver_thread(void *argument)
{
    (void)argument;
    while (!atomic_load(&stop)) {
        crux_config_service_save_report_t report;
        crux_config_service_result_t result = crux_config_service_save(session_of_saver, &report);
        if (result != CRUX_CONFIG_SERVICE_OK && result != CRUX_CONFIG_SERVICE_ERR_BUSY) {
            atomic_fetch_add(&unexpected, 1u);
        }
    }
    return NULL;
}

static void threads_under_load(void)
{
    fixture_start();
    session_of_reader = s_reader;
    session_of_writer = s_writer;
    session_of_saver = s_saver;
    pthread_t threads[4];
    CHECK(pthread_create(&threads[0], NULL, reader_thread, NULL) == 0);
    CHECK(pthread_create(&threads[1], NULL, writer_thread, NULL) == 0);
    CHECK(pthread_create(&threads[2], NULL, control_thread, NULL) == 0);
    CHECK(pthread_create(&threads[3], NULL, saver_thread, NULL) == 0);
    struct timespec pause = {0, 300000000};
    nanosleep(&pause, NULL);
    atomic_store(&stop, true);
    for (size_t i = 0; i < 4; ++i) {
        CHECK(pthread_join(threads[i], NULL) == 0);
    }
    CHECK(atomic_load(&unexpected) == 0);
}

int main(void)
{
    run_case("busy during a held save", busy_during_a_held_save);
    run_case("validator relock", validator_relock);
    run_case("threads under load", threads_under_load);
    return suite_finish("ConfigService.concurrency");
}
