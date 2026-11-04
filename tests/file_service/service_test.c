#include "service_test.h"
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <unistd.h>

const crux_access_session_t SESSION_ALL = {1};
const crux_access_session_t SESSION_OTHER = {2};
const crux_access_session_t SESSION_READ = {3};
const crux_access_session_t SESSION_WRITE = {4};
const crux_access_session_t SESSION_NONE = {5};
const crux_access_session_t SESSION_ENDED = {6};

static unsigned denials;
static unsigned probes;
static bool other_ended;

unsigned fake_access_denials(void)
{
    return denials;
}

unsigned fake_access_probes(void)
{
    return probes;
}

void fake_access_end_other(void)
{
    other_ended = true;
}

static crux_access_result_t decide(crux_access_session_t session, const char *permission,
                                   bool *permitted)
{
    if (session.value == 0 || session.value > SESSION_NONE.value ||
        (session.value == SESSION_OTHER.value && other_ended)) {
        return CRUX_ACCESS_ERR_SESSION_INVALID;
    }
    *permitted = false;
    if (session.value == SESSION_ALL.value || session.value == SESSION_OTHER.value) {
        *permitted = true;
    } else if (session.value == SESSION_READ.value) {
        *permitted = strstr(permission, ".read") != NULL;
    } else if (session.value == SESSION_WRITE.value) {
        *permitted = strstr(permission, ".write") != NULL;
    }
    return CRUX_ACCESS_OK;
}

crux_access_result_t crux_access_check(crux_access_session_t session, const char *permission)
{
    bool permitted = false;
    crux_access_result_t result = decide(session, permission, &permitted);
    if (result != CRUX_ACCESS_OK) {
        return result;
    }
    if (!permitted) {
        denials++;
        return CRUX_ACCESS_ERR_DENIED;
    }
    return CRUX_ACCESS_OK;
}

crux_access_result_t crux_access_permitted(crux_access_session_t session, const char *permission,
                                           bool *permitted)
{
    probes++;
    return decide(session, permission, permitted);
}

crux_access_result_t crux_access_session_user(crux_access_session_t session, char *name,
                                              size_t size)
{
    bool permitted = false;
    crux_access_result_t result = decide(session, "probe", &permitted);
    if (result == CRUX_ACCESS_OK && size > 0) {
        name[0] = '\0';
    }
    return result;
}

static recorded_event_t recorded[64];
static unsigned recorded_count;

void crux_file_service_port_event(crux_file_service_event_t event, crux_access_session_t session,
                                  const char *area, const char *name, const char *new_name,
                                  bool storage_limit)
{
    (void)session;
    assert(recorded_count < sizeof(recorded) / sizeof(recorded[0]));
    recorded_event_t *r = &recorded[recorded_count++];
    r->event = event;
    r->storage_limit = storage_limit;
    (void)snprintf(r->area, sizeof(r->area), "%s", area ? area : "");
    (void)snprintf(r->name, sizeof(r->name), "%s", name ? name : "");
    (void)snprintf(r->new_name, sizeof(r->new_name), "%s", new_name ? new_name : "");
}

unsigned events_count(void)
{
    return recorded_count;
}

const recorded_event_t *events_get(unsigned index)
{
    assert(index < recorded_count);
    return &recorded[index];
}

unsigned events_of(crux_file_service_event_t event)
{
    unsigned count = 0;
    for (unsigned i = 0; i < recorded_count; i++) {
        count += recorded[i].event == event;
    }
    return count;
}

bool events_mention(const char *text)
{
    for (unsigned i = 0; i < recorded_count; i++) {
        if (strstr(recorded[i].area, text) || strstr(recorded[i].name, text) ||
            strstr(recorded[i].new_name, text)) {
            return true;
        }
    }
    return false;
}

static const crux_file_area_t areas[] = {
    {"data", ROOT_DATA, "data.read", "data.write", AREA_MAX_FILE, AREA_MAX_AREA, AREA_MAX_ENTRIES,
     ".filepart", CRUX_FILE_SERVICE_MANAGED | CRUX_FILE_SERVICE_DIRECT},
    {"logs", ROOT_LOGS, "logs.read", NULL, AREA_MAX_FILE, AREA_MAX_AREA, AREA_MAX_ENTRIES, NULL, 0},
    {"web", ROOT_WEB, "web.read", "web.write", AREA_MAX_FILE, AREA_MAX_AREA, AREA_MAX_ENTRIES, NULL,
     CRUX_FILE_SERVICE_MANAGED},
    {"gone", ROOT_GONE, "gone.read", "gone.write", AREA_MAX_FILE, AREA_MAX_AREA, AREA_MAX_ENTRIES,
     NULL, CRUX_FILE_SERVICE_DIRECT},
};
static const crux_file_root_t reserved[] = {ROOT_STORE};
static const crux_file_service_config_t config = {areas, sizeof(areas) / sizeof(areas[0]), reserved,
                                                  1, false};

const crux_file_service_config_t *test_config(void)
{
    return &config;
}

void test_medium_reset(void)
{
    crux_file_fake_reset();
    assert(crux_file_fake_bind_root(ROOT_DATA, "/card/data") == CRUX_FILE_OK);
    assert(crux_file_fake_bind_root(ROOT_LOGS, "/card/logs") == CRUX_FILE_OK);
    assert(crux_file_fake_bind_root(ROOT_WEB, "/card/web") == CRUX_FILE_OK);
    assert(crux_file_fake_bind_root(ROOT_STORE, "/card/store") == CRUX_FILE_OK);
}

void test_start(void)
{
    assert(crux_file_service_start(&config) == CRUX_FILE_SERVICE_OK);
}

void put_raw(crux_file_root_t root, const char *name, const char *text)
{
    crux_file_t *file = NULL;
    size_t count = 0;
    assert(crux_file_open_at(root, name, CRUX_FILE_CREATE_REPLACE, &file) == CRUX_FILE_OK);
    if (*text) {
        assert(crux_file_write(file, text, strlen(text), &count) == CRUX_FILE_OK);
    }
    assert(crux_file_sync(file) == CRUX_FILE_OK);
    assert(crux_file_close(&file) == CRUX_FILE_OK);
}

size_t get_raw(crux_file_root_t root, const char *name, char *value, size_t capacity)
{
    crux_file_t *file = NULL;
    size_t count = 0;
    assert(crux_file_open_at(root, name, CRUX_FILE_READ, &file) == CRUX_FILE_OK);
    assert(crux_file_read(file, value, capacity, &count) == CRUX_FILE_OK);
    assert(crux_file_close(&file) == CRUX_FILE_OK);
    return count;
}

void expect_raw(crux_file_root_t root, const char *name, const char *text)
{
    char value[64];
    size_t count = get_raw(root, name, value, sizeof(value));
    assert(count == strlen(text));
    assert(memcmp(value, text, count) == 0);
}

int run_child(void (*body)(void))
{
    fflush(NULL);
    pid_t child = fork();
    assert(child >= 0);
    if (child == 0) {
        body();
        _exit(0);
    }
    int status = 0;
    assert(waitpid(child, &status, 0) == child);
    /* An assertion in the child is a signal, which no case treats as an expected result. */
    assert(WIFEXITED(status));
    return WEXITSTATUS(status);
}

void run_case(const char *name, void (*body)(void))
{
    test_medium_reset();
    int status = run_child(body);
    if (status != 0) {
        fprintf(stderr, "%s: exit %d\n", name, status);
        abort();
    }
}

void cut_now(void)
{
    _exit(CUT_EXIT);
}

int main(void)
{
    test_paths();
    test_permissions();
    test_capacity();
    test_conflicts();
    test_transfers();
    test_interrupted();
    puts("File service checks passed");
    return 0;
}
