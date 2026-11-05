/* The session table and the session lookup, on the server thread.
 *
 * What a lookup receives, from the review of #184, and the rule here for each:
 *
 * - A NUL in a header value ends the captured value early. Every check compares a whole expected
 *   value with the whole captured one, by length and content, so a captured prefix of a token, of
 *   an Origin or of "1" never passes.
 * - The server captures no empty value, so an empty header of a captured name can stand beside one
 *   with a value, and the adapter's duplicate check does not see it. An empty value reads as no
 *   header here, and every check needs a value, so an empty duplicate can neither pass a check nor
 *   replace the value that the check reads.
 */
#include "web_session_internal.h"

#include <string.h>

typedef struct {
    bool used;
    uint8_t token_hash[CRUX_WEB_SESSION_HASH_SIZE];
    uint8_t csrf_hash[CRUX_WEB_SESSION_HASH_SIZE];
    crux_access_session_t session;
} web_session_slot_t;

static web_session_slot_t web_sessions[CRUX_WEB_SESSION_MAX];
static crux_web_session_config_t web_session_config;

static const char base64url[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";

_Static_assert(CRUX_WEB_SESSION_TOKEN_TEXT == (CRUX_WEB_SESSION_TOKEN_BYTES * 8u + 5u) / 6u,
               "a token's text is its bytes in unpadded base64url");
_Static_assert(CRUX_WEB_SESSION_TOKEN_BYTES * 8u >= 128u,
               "an attempt identifier holds at least 128 bits");

void web_session_wipe(void *buffer, size_t length)
{
    volatile uint8_t *bytes = buffer;

    while (length-- > 0u) {
        *bytes++ = 0u;
    }
}

bool web_session_equal(const void *first, const void *second, size_t length)
{
    const volatile uint8_t *left = first;
    const volatile uint8_t *right = second;
    uint8_t difference = 0u;

    for (size_t index = 0; index < length; ++index) {
        difference |= (uint8_t)(left[index] ^ right[index]);
    }
    return difference == 0u;
}

bool web_session_token_draw(char text[WEB_SESSION_TOKEN_SIZE])
{
    uint8_t bytes[CRUX_WEB_SESSION_TOKEN_BYTES + 2u] = {0};
    size_t out = 0;

    if (!crux_web_session_port_random(bytes, CRUX_WEB_SESSION_TOKEN_BYTES)) {
        web_session_wipe(bytes, sizeof(bytes));
        return false;
    }
    /* Three bytes make four characters. The two zero bytes past the end fill the last group, whose
     * extra characters are not written. */
    for (size_t index = 0; index < CRUX_WEB_SESSION_TOKEN_BYTES; index += 3u) {
        uint32_t group = ((uint32_t)bytes[index] << 16) | ((uint32_t)bytes[index + 1u] << 8) |
                         (uint32_t)bytes[index + 2u];

        for (int shift = 18; shift >= 0 && out < CRUX_WEB_SESSION_TOKEN_TEXT; shift -= 6) {
            text[out++] = base64url[(group >> shift) & 0x3fu];
        }
    }
    text[out] = '\0';
    web_session_wipe(bytes, sizeof(bytes));
    return true;
}

static bool is_base64url(char character)
{
    return (character >= 'A' && character <= 'Z') || (character >= 'a' && character <= 'z') ||
           (character >= '0' && character <= '9') || character == '-' || character == '_';
}

bool web_session_token_form(const char *text)
{
    size_t index = 0;

    if (text == NULL) {
        return false;
    }
    for (; index < CRUX_WEB_SESSION_TOKEN_TEXT; ++index) {
        if (!is_base64url(text[index])) {
            return false;
        }
    }
    return text[index] == '\0';
}

bool web_session_token_hash(const char *text, uint8_t hash[CRUX_WEB_SESSION_HASH_SIZE])
{
    return crux_web_session_port_hash((const uint8_t *)text, CRUX_WEB_SESSION_TOKEN_TEXT, hash);
}

void web_session_table_reset(void)
{
    web_session_wipe(web_sessions, sizeof(web_sessions));
}

/* Whether crux_access still holds the session. No event, no timer, no state. */
static bool still_valid(crux_access_session_t session)
{
    bool permitted = false;

    return crux_access_permitted(session, CRUX_ACCESS_PERMISSION_SELF, &permitted) !=
           CRUX_ACCESS_ERR_SESSION_INVALID;
}

static void clear_slot(web_session_slot_t *slot)
{
    web_session_wipe(slot, sizeof(*slot));
}

bool web_session_table_insert(const uint8_t token_hash[CRUX_WEB_SESSION_HASH_SIZE],
                              const uint8_t csrf_hash[CRUX_WEB_SESSION_HASH_SIZE],
                              crux_access_session_t session)
{
    web_session_slot_t *free_slot = NULL;

    for (size_t index = 0; index < CRUX_WEB_SESSION_MAX; ++index) {
        web_session_slot_t *slot = &web_sessions[index];

        if (slot->used && !still_valid(slot->session)) {
            clear_slot(slot);
        }
        if (!slot->used && free_slot == NULL) {
            free_slot = slot;
        }
    }
    if (free_slot == NULL) {
        return false;
    }
    free_slot->used = true;
    memcpy(free_slot->token_hash, token_hash, CRUX_WEB_SESSION_HASH_SIZE);
    memcpy(free_slot->csrf_hash, csrf_hash, CRUX_WEB_SESSION_HASH_SIZE);
    free_slot->session = session;
    return true;
}

void web_session_table_remove(crux_access_session_t session)
{
    for (size_t index = 0; index < CRUX_WEB_SESSION_MAX; ++index) {
        if (web_sessions[index].used && web_sessions[index].session.value == session.value) {
            clear_slot(&web_sessions[index]);
        }
    }
}

size_t web_session_table_count(void)
{
    size_t count = 0;

    for (size_t index = 0; index < CRUX_WEB_SESSION_MAX; ++index) {
        count += web_sessions[index].used ? 1u : 0u;
    }
    return count;
}

/* Every slot is compared, so the time does not say which one matched. */
static web_session_slot_t *find_by_hash(const uint8_t hash[CRUX_WEB_SESSION_HASH_SIZE])
{
    web_session_slot_t *found = NULL;

    for (size_t index = 0; index < CRUX_WEB_SESSION_MAX; ++index) {
        bool match =
            web_session_equal(web_sessions[index].token_hash, hash, CRUX_WEB_SESSION_HASH_SIZE);

        if (match && web_sessions[index].used && found == NULL) {
            found = &web_sessions[index];
        }
    }
    return found;
}

static bool present(const char *value)
{
    return value != NULL && value[0] != '\0';
}

/* A name or an address, with an optional port: nothing that could close or extend the Origin. */
static bool host_form(const char *host)
{
    size_t length = 0;

    for (; host[length] != '\0'; ++length) {
        char character = host[length];

        if (!((character >= 'a' && character <= 'z') || (character >= 'A' && character <= 'Z') ||
              (character >= '0' && character <= '9') || character == '.' || character == '-' ||
              character == ':' || character == '[' || character == ']')) {
            return false;
        }
    }
    return length != 0u && length <= 128u;
}

static bool same_text(const char *first, const char *second)
{
    size_t length = strlen(second);

    return strlen(first) == length && memcmp(first, second, length) == 0;
}

bool web_session_origin_accepted(const crux_web_request_head_t *head)
{
    const char *origin = head->origin;
    const char *scheme = web_session_config.scheme;
    size_t scheme_length;

    if (!present(origin) || scheme == NULL) {
        return false;
    }
    if (web_session_config.origin_count != 0u) {
        for (size_t index = 0; index < web_session_config.origin_count; ++index) {
            if (same_text(origin, web_session_config.origins[index])) {
                return true;
            }
        }
        return false;
    }
    /* scheme, "://", Host, compared whole and exactly, so case and a trailing dot count. */
    if (!present(head->host) || !host_form(head->host)) {
        return false;
    }
    scheme_length = strlen(scheme);
    return strncmp(origin, scheme, scheme_length) == 0 &&
           strncmp(origin + scheme_length, "://", 3u) == 0 &&
           same_text(origin + scheme_length + 3u, head->host);
}

/* The crux_session value from a Cookie header, if it appears exactly once, with exactly that name.
 * RFC 6265 pairs are separated by "; ". A quoted value is not the token's form, and fails it. */
static bool session_cookie(const char *header, char value[WEB_SESSION_TOKEN_SIZE])
{
    static const char name[] = "crux_session";
    unsigned found = 0u;

    if (!present(header)) {
        return false;
    }
    while (*header != '\0') {
        const char *pair;
        size_t length;

        while (*header == ' ' || *header == '\t' || *header == ';') {
            ++header;
        }
        pair = header;
        length = strcspn(pair, ";");
        header += length;
        if (length > sizeof(name) && memcmp(pair, name, sizeof(name) - 1u) == 0 &&
            pair[sizeof(name) - 1u] == '=') {
            size_t value_length = length - sizeof(name);

            /* Trailing spaces before the separator belong to no value. */
            while (value_length > 0u && (pair[sizeof(name) + value_length - 1u] == ' ' ||
                                         pair[sizeof(name) + value_length - 1u] == '\t')) {
                --value_length;
            }
            ++found;
            if (value_length != CRUX_WEB_SESSION_TOKEN_TEXT) {
                value[0] = '\0';
            } else {
                memcpy(value, pair + sizeof(name), value_length);
                value[value_length] = '\0';
            }
        }
    }
    return found == 1u && web_session_token_form(value);
}

static bool csrf_matches(const web_session_slot_t *slot, const char *header)
{
    uint8_t hash[CRUX_WEB_SESSION_HASH_SIZE];
    bool matched;

    if (!web_session_token_form(header) || !web_session_token_hash(header, hash)) {
        return false;
    }
    matched = web_session_equal(hash, slot->csrf_hash, sizeof(hash));
    web_session_wipe(hash, sizeof(hash));
    return matched;
}

crux_web_auth_t crux_web_session_authenticate(const crux_web_request_head_t *head,
                                              crux_access_session_t *session, void *context)
{
    char token[WEB_SESSION_TOKEN_SIZE] = {0};
    uint8_t hash[CRUX_WEB_SESSION_HASH_SIZE];
    web_session_slot_t *slot;
    bool hashed;
    crux_access_result_t touched;

    (void)context;
    if (head == NULL || session == NULL) {
        return CRUX_WEB_AUTH_NOT_READY;
    }
    if (!session_cookie(head->cookie, token)) {
        web_session_wipe(token, sizeof(token));
        return CRUX_WEB_AUTH_NO_SESSION;
    }
    hashed = web_session_token_hash(token, hash);
    web_session_wipe(token, sizeof(token));
    if (!hashed) {
        return CRUX_WEB_AUTH_NOT_READY;
    }
    slot = find_by_hash(hash);
    web_session_wipe(hash, sizeof(hash));
    if (slot == NULL) {
        return CRUX_WEB_AUTH_NO_SESSION;
    }
    if (!still_valid(slot->session)) {
        /* Ended by logout elsewhere, expiry, or a change to the account or its role. */
        clear_slot(slot);
        return CRUX_WEB_AUTH_NO_SESSION;
    }
    if (head->method != CRUX_WEB_GET) {
        if (!web_session_origin_accepted(head)) {
            return CRUX_WEB_AUTH_ORIGIN_REJECTED;
        }
        if (!csrf_matches(slot, head->xsrf_token)) {
            return CRUX_WEB_AUTH_CSRF_REJECTED;
        }
        /* State-changing requests refresh the idle timer, and GET does not, so a page that polls
         * cannot keep a session alive. */
        touched = crux_access_touch(slot->session);
        if (touched == CRUX_ACCESS_ERR_SESSION_INVALID) {
            clear_slot(slot);
            return CRUX_WEB_AUTH_NO_SESSION;
        }
        if (touched != CRUX_ACCESS_OK) {
            return CRUX_WEB_AUTH_NOT_READY;
        }
    }
    *session = slot->session;
    return CRUX_WEB_AUTH_OK;
}

/* Plain HTTP exists only in a native_sim test image, whose build defines this. */
static bool scheme_allowed(const char *scheme)
{
#if defined(CRUX_WEB_SESSION_PLAIN_HTTP_FOR_TESTS)
    if (strcmp(scheme, "http") == 0) {
        return true;
    }
#endif
    return strcmp(scheme, "https") == 0;
}

crux_web_result_t crux_web_session_init(const crux_web_session_config_t *config)
{
    if (config == NULL || config->scheme == NULL || !scheme_allowed(config->scheme) ||
        (config->origin_count != 0u && config->origins == NULL)) {
        return CRUX_WEB_ERR_INVALID_ARGUMENT;
    }
    for (size_t index = 0; index < config->origin_count; ++index) {
        if (!present(config->origins[index])) {
            return CRUX_WEB_ERR_INVALID_ARGUMENT;
        }
    }
    web_session_config = *config;
    web_session_table_reset();
    web_session_attempts_reset();
    return web_session_attempts_ready() ? CRUX_WEB_OK : CRUX_WEB_ERR_FAILED;
}
