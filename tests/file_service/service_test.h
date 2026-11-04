#ifndef CRUX_FILE_SERVICE_TEST_H
#define CRUX_FILE_SERVICE_TEST_H
#include "fake_file.h"
#include "file_service_port.h"
#include <assert.h>
#include <crux/file_service.h>
#include <string.h>

/* Sessions of the fake access double, which answers from a fixed table. */
extern const crux_access_session_t SESSION_ALL;   /* Every permission below. */
extern const crux_access_session_t SESSION_OTHER; /* The same, as a second client. */
extern const crux_access_session_t SESSION_READ;  /* data.read, logs.read, web.read. */
extern const crux_access_session_t SESSION_WRITE; /* data.write, web.write. */
extern const crux_access_session_t SESSION_NONE;  /* Valid, with no permission here. */
extern const crux_access_session_t SESSION_ENDED; /* Never valid. */

/* Denials that crux_access_check reported, which become events in a real composition. */
unsigned fake_access_denials(void);
/* Whether crux_access_permitted was asked at least once. */
unsigned fake_access_probes(void);
/* End SESSION_OTHER, as a logout during a transfer would. */
void fake_access_end_other(void);

typedef struct {
    crux_file_service_event_t event;
    char area[16];
    char name[64];
    char new_name[64];
    bool storage_limit;
} recorded_event_t;

unsigned events_count(void);
const recorded_event_t *events_get(unsigned index);
unsigned events_of(crux_file_service_event_t event);
/* Whether text appears in any field of any event, to prove that a rejected name was not recorded.
 */
bool events_mention(const char *text);

#define AREA_MAX_FILE 1000u
#define AREA_MAX_AREA 2000u
#define AREA_MAX_ENTRIES 5u

/* data: both write modes, partial files. web: managed only. logs: read-only. gone: root unbound. */
enum { ROOT_DATA = 0, ROOT_LOGS = 1, ROOT_WEB = 2, ROOT_GONE = 3, ROOT_STORE = 7 };

/* Empty the fake and bind the test roots. Call it only from the runner. */
void test_medium_reset(void);
/* Start the service on the test table, which must succeed. */
void test_start(void);
const crux_file_service_config_t *test_config(void);

/* Write a file straight through File, for a state a case begins from. */
void put_raw(crux_file_root_t root, const char *name, const char *text);
/* The content of a file straight through File. */
size_t get_raw(crux_file_root_t root, const char *name, char *value, size_t capacity);
void expect_raw(crux_file_root_t root, const char *name, const char *text);

/* Run a case in a forked child, so that each one starts the service from nothing. */
void run_case(const char *name, void (*body)(void));
/* Run part of a case in its own child and return its exit status. */
int run_child(void (*body)(void));
#define CUT_EXIT 77
void cut_now(void);

void test_permissions(void);
void test_paths(void);
void test_capacity(void);
void test_conflicts(void);
void test_transfers(void);
void test_interrupted(void);
#endif
