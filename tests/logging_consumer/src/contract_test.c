#include "runtime.h"

#include <assert.h>
#include <limits.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

/* Public-symbol doubles observe consumer policy without modelling an OS,
 * queue, filtering or delivery. Actual-provider tests belong to composition. */
static unsigned writes;
static unsigned setters;
static crux_status_t write_result;
static crux_log_level_t last_level;
static char last_tag[25];
static char last_text[257];

crux_status_t crux_log_set_level(crux_log_level_t maximum_level)
{
    (void)maximum_level;
    ++setters;
    return CRUX_OK;
}

crux_status_t crux_log_write(crux_log_level_t level, const char *tag, const char *text)
{
    assert(tag != NULL && text != NULL);
    assert(strlen(tag) > 0 && strlen(tag) < sizeof(last_tag));
    assert(strlen(text) < sizeof(last_text));
    assert(strchr(text, '\r') == NULL && strchr(text, '\n') == NULL);
    ++writes;
    last_level = level;
    memcpy(last_tag, tag, strlen(tag) + 1);
    memcpy(last_text, text, strlen(text) + 1);
    return write_result;
}

typedef crux_status_t (*record_fn)(uint32_t sequence, int code);

static void check_consumer(record_fn record, const char *tag)
{
    writes = 0;
    setters = 0;
    write_result = CRUX_OK;
    assert(record(42, 0) == CRUX_OK);
    assert(writes == 1 && setters == 0);
    assert(last_level == CRUX_LOG_INFO);
    assert(strcmp(last_tag, tag) == 0);
    assert(strcmp(last_text, "sequence=42 code=0") == 0);

    assert(record(43, -7) == CRUX_OK);
    assert(writes == 2 && setters == 0);
    assert(last_level == CRUX_LOG_ERROR);
    assert(strcmp(last_text, "sequence=43 code=-7") == 0);

    assert(record(0, 7) == CRUX_OK);
    assert(writes == 3 && setters == 0);
    assert(last_level == CRUX_LOG_ERROR);
    assert(strcmp(last_text, "sequence=0 code=7") == 0);

    /* Wide values must remain complete rather than becoming a valid prefix.
     * uint32_t is fixed-width; int's width remains implementation-defined. */
    char minimum_code[sizeof(int) * CHAR_BIT + 1];
    int count = snprintf(minimum_code, sizeof(minimum_code), "%d", INT_MIN);
    assert(count > 0 && (size_t)count < sizeof(minimum_code));
    assert(record(UINT32_MAX, INT_MIN) == CRUX_OK);
    assert(writes == 4 && setters == 0);
    assert(last_level == CRUX_LOG_ERROR);
    assert(strncmp(last_text, "sequence=4294967295 code=", 25) == 0);
    assert(strcmp(last_text + 25, minimum_code) == 0);

    const crux_status_t failures[] = {CRUX_ERR_IO, CRUX_ERR_NOT_READY, CRUX_ERR_NOT_SUPPORTED};
    for (size_t index = 0; index < sizeof(failures) / sizeof(failures[0]); ++index) {
        unsigned before = writes;
        write_result = failures[index];
        assert(record(44, -1) == failures[index]);
        assert(writes == before + 1 && setters == 0);
        assert(last_level == CRUX_LOG_ERROR);
        assert(strcmp(last_text, "sequence=44 code=-1") == 0);
    }
}

int main(void)
{
    check_consumer(logging_runtime_record, "runtime.c");
    check_consumer(logging_cpp_record, "runtime.cpp");
    return 0;
}
