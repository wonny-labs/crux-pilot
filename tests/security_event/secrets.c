/* Known secrets never appear in a record, a line or a read result, using the real emitters. This
 * runs Access's own secret suite, which reaches every port event with known passwords, keys and a
 * password typed into the name field, and inspects what reached the ring and the log. It draws from
 * the live account table the salts and verifiers that exist at each moment. */
#include "joined.h"

int access_secrets_main(void);

#define KNOWN_TEXT "known-secret"
/* The fill bytes of the known keys in tests/access/secrets.c. */
#define KEY_ONE_BYTE 0xDEu
#define KEY_TWO_BYTE 0xCAu
#define WINDOW 8u

static bool contains(const void *haystack, size_t haystack_size, const void *needle,
                     size_t needle_size)
{
    const uint8_t *bytes = haystack;
    if (haystack_size < needle_size) {
        return false;
    }
    for (size_t offset = 0; offset <= haystack_size - needle_size; ++offset) {
        if (memcmp(&bytes[offset], needle, needle_size) == 0) {
            return true;
        }
    }
    return false;
}

static void expect_clean_bytes(const void *memory, size_t size)
{
    uint8_t key_one[WINDOW];
    uint8_t key_two[WINDOW];
    memset(key_one, KEY_ONE_BYTE, sizeof(key_one));
    memset(key_two, KEY_TWO_BYTE, sizeof(key_two));
    CHECK(!contains(memory, size, KNOWN_TEXT, strlen(KNOWN_TEXT)));
    CHECK(!contains(memory, size, key_one, sizeof(key_one)));
    CHECK(!contains(memory, size, key_two, sizeof(key_two)));
    /* Every salt and verifier in the live table. Nothing holds the table's locks at this point. */
    for (size_t index = 0; index < crux_access_state.table.count; ++index) {
        const access_account_t *account = &crux_access_state.table.accounts[index];
        CHECK(!contains(memory, size, account->salt, WINDOW));
        CHECK(!contains(memory, size, account->derived, WINDOW));
    }
}

static size_t records_seen, lines_seen;

/* Runs in the port, after the provider has emitted. */
static void inspect_after_event(void *context)
{
    (void)context;
    /* The ring in full, including what a read would copy. */
    expect_clean_bytes(crux_security_event_state.slots, sizeof(crux_security_event_state.slots));
    uint32_t cursor = crux_security_event_state.next_sequence - 1u;
    crux_security_event_record_t record;
    if (crux_security_event_read_unchecked(&cursor, &record) == CRUX_SECURITY_READ_OK) {
        expect_clean_bytes(&record, sizeof(record));
        /* No name outside the known accounts. */
        CHECK(strstr(record.user, "secret") == NULL);
        CHECK(strstr(record.subject, "secret") == NULL);
        records_seen += 1;
    }
    /* Then its line, at once, so that the ring never overflows and nothing escapes inspection. */
    size_t before = event_test_line_count();
    event_test_drain_all();
    for (size_t index = before; index < event_test_line_count(); ++index) {
        const char *text = event_test_line(index)->text;
        expect_clean_bytes(text, strlen(text));
        CHECK(strstr(text, "secret") == NULL);
        CHECK(strstr(text, "password-") == NULL);
        lines_seen += 1;
    }
    event_test_lines_clear();
}

int main(void)
{
    event_test_boot();
    joined_port_hook(inspect_after_event, NULL);
    (void)access_secrets_main();
    joined_port_hook(NULL, NULL);

    /* It really ran through the path: each record became a line, with nothing lost. */
    CHECK(records_seen > 60u);
    CHECK(lines_seen == records_seen);
    crux_security_event_stats_t stats = event_test_stats();
    CHECK(stats.emitted == records_seen);
    CHECK(stats.delivered == records_seen);
    CHECK(stats.dropped == 0);
    /* The access core's checks hold: nothing it emitted failed a field rule. */
    CHECK(stats.invalid_fields == 0);
    /* Totals since the start, which include the Access suite's own checks. */
    int access_status = access_test_finish("Security event secret absence");
    int event_status = event_test_finish("Security event secret absence, support");
    return access_status != 0 || event_status != 0 ? 1 : 0;
}
