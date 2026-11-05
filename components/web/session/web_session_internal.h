/* Private to the session module and its Host tests. */
#ifndef CRUX_WEB_SESSION_INTERNAL_H
#define CRUX_WEB_SESSION_INTERNAL_H

#include "crux_web_session.h"
#include "web_session_port.h"

#include <crux_access_adapter.h>

#ifndef CRUX_WEB_SESSION_MAX
/* Web sessions held at once. No more can be live than crux_access holds, 8 by default. */
#define CRUX_WEB_SESSION_MAX 8
#endif
#ifndef CRUX_WEB_SESSION_ATTEMPTS
/* Attempts held at once: one queued or running, and results waiting for their poll. */
#define CRUX_WEB_SESSION_ATTEMPTS 4
#endif
#ifndef CRUX_WEB_SESSION_ATTEMPT_TTL_MS
/* How long an attempt waits for its worker or its poll before it ends on its own. */
#define CRUX_WEB_SESSION_ATTEMPT_TTL_MS 20000u
#endif
#ifndef CRUX_WEB_SESSION_LOCK_WAIT_MS
/* The longest the server thread waits for the attempt lock. The worker holds it only to copy. */
#define CRUX_WEB_SESSION_LOCK_WAIT_MS 20u
#endif
#ifndef CRUX_WEB_SESSION_WORKER_WAIT_MS
/* How often the worker wakes with nothing signalled, to end attempts that expired. */
#define CRUX_WEB_SESSION_WORKER_WAIT_MS 1000u
#endif

/* A password as the attempt holds it: one byte more than crux_access accepts, so that a longer one
 * still reaches crux_access, which refuses it by its length and reads none of it. */
#define WEB_SESSION_SECRET_SIZE (CRUX_ACCESS_PASSWORD_MAX + 2u)
#define WEB_SESSION_TOKEN_SIZE (CRUX_WEB_SESSION_TOKEN_TEXT + 1u)

/* Zeroes memory in a way the compiler keeps. */
void web_session_wipe(void *buffer, size_t length);
/* Compares every byte whatever the first difference. */
bool web_session_equal(const void *first, const void *second, size_t length);

/* Draws CRUX_WEB_SESSION_TOKEN_BYTES from the port and writes them as terminated base64url. Session
 * tokens, CSRF tokens and attempt identifiers all come from here. */
bool web_session_token_draw(char text[WEB_SESSION_TOKEN_SIZE]);
/* Whether text is exactly one token's form: 43 base64url characters and nothing else. */
bool web_session_token_form(const char *text);
bool web_session_token_hash(const char *text, uint8_t hash[CRUX_WEB_SESSION_HASH_SIZE]);

/* The session table, on the server thread only. */
void web_session_table_reset(void);
/* False when no slot is free once ended sessions are cleared. */
bool web_session_table_insert(const uint8_t token_hash[CRUX_WEB_SESSION_HASH_SIZE],
                              const uint8_t csrf_hash[CRUX_WEB_SESSION_HASH_SIZE],
                              crux_access_session_t session);
void web_session_table_remove(crux_access_session_t session);
size_t web_session_table_count(void);

/* Whether head's Origin is the one this server accepts, compared whole. */
bool web_session_origin_accepted(const crux_web_request_head_t *head);

/* What an attempt was for. */
typedef enum { WEB_ATTEMPT_LOGIN, WEB_ATTEMPT_CHANGE } web_attempt_kind_t;

/* What a poll found. */
typedef enum {
    WEB_POLL_NOT_FOUND,
    WEB_POLL_PENDING,
    WEB_POLL_DONE /* The attempt has ended, and the result is the caller's. */
} web_poll_t;

typedef struct {
    web_attempt_kind_t kind;
    crux_access_result_t result;
    uint32_t retry_after_ms;
    crux_access_session_t session; /* A login's new session. */
    char token[WEB_SESSION_TOKEN_SIZE];
    char csrf[WEB_SESSION_TOKEN_SIZE];
    uint8_t token_hash[CRUX_WEB_SESSION_HASH_SIZE];
    uint8_t csrf_hash[CRUX_WEB_SESSION_HASH_SIZE];
} web_attempt_result_t;

/* What the request handlers pass in. The secrets are copied, and the caller clears its own. */
typedef struct {
    web_attempt_kind_t kind;
    const char *user; /* A login's. An empty name is one that no account can hold. */
    const char *secret;
    size_t secret_length;
    const char *new_secret; /* A change's. */
    size_t new_secret_length;
    crux_access_origin_t origin;
    crux_access_session_t session; /* A change's. */
} web_attempt_request_t;

typedef enum {
    WEB_QUEUE_OK,
    WEB_QUEUE_BUSY /* One is already queued or running, or no slot or identifier: nothing queued. */
} web_queue_t;

void web_session_attempts_reset(void);
bool web_session_attempts_ready(void);
/* Queues an attempt and writes its identifier. Server thread. */
web_queue_t web_session_attempt_queue(const web_attempt_request_t *request,
                                      char id[WEB_SESSION_TOKEN_SIZE]);
/* Looks an attempt up by its whole identifier. On DONE the attempt is gone and result holds it,
 * which the caller clears. Server thread. */
web_poll_t web_session_attempt_poll(const char *id, web_attempt_result_t *result);
/* Runs the queued attempt, if there is one, on the calling thread. True if one ran. The worker
 * thread's loop calls this, and a Host test may call it in place of the worker. */
bool web_session_work_once(void);
/* Ends attempts older than their limit, and logs out a session that nobody collected. */
void web_session_attempts_expire(void);
void web_session_count_busy(void);
void web_session_count_session(void);

/* A JSON object of string members, each named once. Anything else in the body is malformed. */
typedef struct {
    const char *name;
    char *buffer;     /* Receives the decoded value, terminated, cut at size - 1. */
    size_t size;
    size_t length;    /* Decoded length, which can exceed size - 1. */
    bool seen;
    bool holds_nul;   /* The value held \u0000. */
} web_json_field_t;

/* True when the body is one object whose members are exactly these fields, in any order. */
bool web_json_parse_strings(const uint8_t *body, size_t length, web_json_field_t *fields,
                            size_t count);

#endif /* CRUX_WEB_SESSION_INTERNAL_H */
