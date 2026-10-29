#include "logging_port.h"
#include <assert.h>
#include <crux/logging.h>
#include <stdio.h>
#include <string.h>
static unsigned emitted;
static bool allowed = true;
static crux_status_t native_status = CRUX_OK;
static crux_log_level_t observed;
static char saved_tag[25], saved_text[257];
bool crux_log_context_allowed(void)
{
    return allowed;
}
crux_status_t crux_log_impl_write(crux_log_level_t level, const char *tag, const char *text)
{
    ++emitted;
    observed = level;
    memcpy(saved_tag, tag, strlen(tag) + 1);
    memcpy(saved_text, text, strlen(text) + 1);
    return native_status;
}
static void validation(void)
{
    assert(crux_log_write(CRUX_LOG_OFF, "app", "x") == CRUX_ERR_INVALID_ARGUMENT);
    // NOLINTNEXTLINE(clang-analyzer-optin.core.EnumCastOutOfRange): invalid-input probe
    assert(crux_log_write((crux_log_level_t)99, "app", "x") == CRUX_ERR_INVALID_ARGUMENT);
    assert(crux_log_write(CRUX_LOG_INFO, NULL, "x") == CRUX_ERR_INVALID_ARGUMENT);
    assert(crux_log_write(CRUX_LOG_INFO, "app", NULL) == CRUX_ERR_INVALID_ARGUMENT);
    assert(crux_log_write(CRUX_LOG_INFO, "", "x") == CRUX_ERR_INVALID_ARGUMENT);
    assert(crux_log_write(CRUX_LOG_INFO, "bad tag", "x") == CRUX_ERR_INVALID_ARGUMENT);
    assert(crux_log_write(CRUX_LOG_INFO, "app", "bad\nline") == CRUX_ERR_INVALID_ARGUMENT);
    assert(crux_log_write(CRUX_LOG_INFO, "app", "bad\rline") == CRUX_ERR_INVALID_ARGUMENT);
    char tag[26], text[258];
    memset(tag, 'T', 25);
    tag[25] = 0;
    memset(text, 'X', 257);
    text[257] = 0;
    assert(crux_log_write(CRUX_LOG_INFO, tag, "x") == CRUX_ERR_OUT_OF_RANGE);
    assert(crux_log_write(CRUX_LOG_INFO, "app", text) == CRUX_ERR_OUT_OF_RANGE);
    assert(emitted == 0);
    tag[24] = 0;
    text[256] = 0;
    assert(crux_log_write(CRUX_LOG_INFO, tag, text) == CRUX_OK);
    assert(strlen(saved_tag) == 24 && strlen(saved_text) == 256);
    assert(crux_log_write(CRUX_LOG_INFO, "a-Z_9.x", "") == CRUX_OK);
}
static void filtering(void)
{
    assert(crux_log_write(CRUX_LOG_DEBUG, "app", "hidden") == CRUX_OK);
    assert(emitted == 0);
    assert(crux_log_write(CRUX_LOG_INFO, "app", "visible") == CRUX_OK);
    assert(emitted == 1 && observed == CRUX_LOG_INFO);
    assert(crux_log_set_level(CRUX_LOG_WARN) == CRUX_OK);
    assert(crux_log_write(CRUX_LOG_INFO, "app", "hidden") == CRUX_OK);
    assert(crux_log_write(CRUX_LOG_WARN, "app", "warning") == CRUX_OK);
    assert(crux_log_write(CRUX_LOG_ERROR, "app", "error") == CRUX_OK);
    assert(emitted == 3);
    // NOLINTNEXTLINE(clang-analyzer-optin.core.EnumCastOutOfRange): invalid-input probe
    assert(crux_log_set_level((crux_log_level_t)99) == CRUX_ERR_INVALID_ARGUMENT);
    assert(crux_log_write(CRUX_LOG_INFO, "app", "still hidden") == CRUX_OK && emitted == 3);
    assert(crux_log_set_level(CRUX_LOG_OFF) == CRUX_OK);
    assert(crux_log_write(CRUX_LOG_ERROR, "app", "off") == CRUX_OK && emitted == 3);
    assert(crux_log_write(CRUX_LOG_INFO, "app", "bad\n") == CRUX_ERR_INVALID_ARGUMENT);
    assert(crux_log_set_level(CRUX_LOG_DEBUG) == CRUX_OK);
    assert(crux_log_write(CRUX_LOG_DEBUG, "app", "debug") == CRUX_OK && emitted == 4);
}
static void lifetime(void)
{
    char tag[] = "runtime.gc", text[] = "value=42 100% %s";
    assert(crux_log_write(CRUX_LOG_WARN, tag, text) == CRUX_OK);
    memset(tag, '?', sizeof(tag) - 1);
    memset(text, '?', sizeof(text) - 1);
    assert(strcmp(saved_tag, "runtime.gc") == 0);
    assert(strcmp(saved_text, "value=42 100% %s") == 0);
}
static void failures(void)
{
    native_status = CRUX_ERR_IO;
    assert(crux_log_write(CRUX_LOG_ERROR, "app", "failed") == CRUX_ERR_IO);
    native_status = CRUX_ERR_NOT_READY;
    assert(crux_log_write(CRUX_LOG_ERROR, "app", "unready") == CRUX_ERR_NOT_READY);
    assert(emitted == 2);
    allowed = false;
    assert(crux_log_write(CRUX_LOG_ERROR, "app", "context") == CRUX_ERR_NOT_SUPPORTED);
    assert(crux_log_set_level(CRUX_LOG_DEBUG) == CRUX_ERR_NOT_SUPPORTED);
    assert(emitted == 2);
}
int main(int argc, char **argv)
{
    assert(argc == 2);
    if (!strcmp(argv[1], "validation"))
        validation();
    else if (!strcmp(argv[1], "filtering"))
        filtering();
    else if (!strcmp(argv[1], "lifetime"))
        lifetime();
    else if (!strcmp(argv[1], "failures"))
        failures();
    else
        assert(false);
    puts("logging common contract passed");
    return 0;
}
