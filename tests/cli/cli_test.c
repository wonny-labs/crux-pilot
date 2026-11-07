#include "cli_test.h"

unsigned cli_test_checks, cli_test_failures;

int cli_test_finish(const char *suite)
{
    printf("%s: %u checks, %u failures\n", suite, cli_test_checks, cli_test_failures);
    return cli_test_failures == 0 ? 0 : 1;
}

bool cli_test_contains(const void *memory, size_t size, const char *needle)
{
    const char *bytes = memory;
    size_t length = strlen(needle);
    if (length == 0 || length > size) {
        return false;
    }
    for (size_t index = 0; index + length <= size; ++index) {
        if (memcmp(bytes + index, needle, length) == 0) {
            return true;
        }
    }
    return false;
}
