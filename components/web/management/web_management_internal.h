/* Private to the management routes and their Host tests. */
#ifndef CRUX_WEB_MANAGEMENT_INTERNAL_H
#define CRUX_WEB_MANAGEMENT_INTERNAL_H

#include "crux_web_management.h"

#include <crux/config_service.h>
#include <crux/management_status.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifndef CRUX_WEB_MANAGEMENT_LOCK_WAIT_MS
/* The longest the server thread waits for the save state's lock. The worker holds it only to copy.
 */
#define CRUX_WEB_MANAGEMENT_LOCK_WAIT_MS 20u
#endif
#ifndef CRUX_WEB_MANAGEMENT_WORKER_WAIT_MS
/* How long the worker waits for its signal, and for the lock when it needs it. */
#define CRUX_WEB_MANAGEMENT_WORKER_WAIT_MS 1000u
#endif

/* A parameter's key is 1 to 30 characters, and this holds one with its terminator. */
#define WEB_MGMT_NAME_SIZE 32u

/* A bounded writer over a caller's buffer. Once it has overflowed it writes nothing more, and the
 * caller tests overflow once, at the end. */
typedef struct {
    char *data;
    size_t size;
    size_t used;
    bool overflow;
} web_mgmt_writer_t;

void web_mgmt_writer_init(web_mgmt_writer_t *writer, char *data, size_t size);
void web_mgmt_write(web_mgmt_writer_t *writer, const char *text);
void web_mgmt_write_u32(web_mgmt_writer_t *writer, uint32_t value);
/* A JSON string, quoted, with `"` and `\` escaped and every control character as \u00XX. */
void web_mgmt_write_string(web_mgmt_writer_t *writer, const char *text, size_t length);

/* What a request body's one member can be. */
typedef enum {
    WEB_VALUE_BOOL,
    WEB_VALUE_NUMBER, /* Any JSON number. number_ok says whether it is a u32. */
    WEB_VALUE_STRING
} web_value_kind_t;

typedef struct {
    web_value_kind_t kind;
    bool boolean;
    bool number_ok;  /* Digits only, no leading zero, from 0 to 4294967295. */
    uint32_t number; /* Meaningful when number_ok. */
    char *text;    /* A string's decoded bytes, terminated, cut at size - 1. May hold NUL bytes. */
    size_t size;   /* Size of text. */
    size_t length; /* The decoded length, which can exceed size - 1. */
} web_value_t;

/* Reads the body `{"value": V}`, where V is true, false, a number or a string, and nothing else:
 * no other member, no repeated member, no null, array or object, no text after the object. Escapes
 * are decoded, including surrogate pairs, and a lone surrogate or a raw control character is
 * malformed. The reader allocates nothing and does not recurse. text may be NULL with size zero. */
bool web_mgmt_parse_update(const uint8_t *body, size_t length, web_value_t *value);

/* The save that the worker runs, and what the list shows of it. */
typedef enum {
    WEB_SAVE_NONE,
    WEB_SAVE_QUEUED,
    WEB_SAVE_RUNNING,
    WEB_SAVE_SAVED,
    WEB_SAVE_FAILED
} web_save_state_t;

typedef struct {
    web_save_state_t state;
    const char *code; /* With WEB_SAVE_FAILED, a code of the specification. Otherwise NULL. */
} web_save_view_t;

typedef enum {
    WEB_SAVE_QUEUE_OK,
    WEB_SAVE_QUEUE_BUSY,     /* One is queued or running, or the lock did not come in time. */
    WEB_SAVE_QUEUE_NOT_READY /* The worker's lock and signal do not exist. */
} web_save_queue_t;

/* Queues a save for the session, on the server thread. */
web_save_queue_t web_mgmt_save_queue(crux_access_session_t session);
/* The latest save. A lock that does not come in time reads as pending, the state it is likely in.
 */
web_save_view_t web_mgmt_save_view(void);
/* Runs the queued save, if there is one, on the calling thread. True if one ran. The worker
 * thread's loop calls this, and a Host test may call it in place of the worker. */
bool web_mgmt_work_once(void);
/* The code of the specification for a service result of a save, and for its report. */
const char *web_mgmt_save_code(crux_config_service_result_t result,
                               crux_config_result_t failed_result);

#ifdef CRUX_WEB_MANAGEMENT_TESTING
/* Whether the handlers' buffers hold these bytes, for a Host test of what an update leaves. */
bool web_management_routes_hold_bytes(const void *bytes, size_t length);
#endif

#endif /* CRUX_WEB_MANAGEMENT_INTERNAL_H */
