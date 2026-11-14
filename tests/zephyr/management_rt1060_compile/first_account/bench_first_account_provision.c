#include "bench_first_account_provision.h"

#include <crux_access_adapter.h>

crux_access_result_t crux_bench_first_account_provision(const char *user, const char *role,
                                                        const uint8_t *file, size_t file_length)
{
    static const crux_access_origin_t serial = {.channel = CRUX_ACCESS_CHANNEL_SERIAL};
    if (user == NULL || role == NULL || file == NULL) {
        return CRUX_ACCESS_ERR_INVALID_ARGUMENT;
    }
    /* One newline ends the line of the file, and it is not part of the password. */
    size_t length = file_length;
    if (length > 0u && file[length - 1u] == (uint8_t)'\n') {
        length -= 1u;
    }
    if (length < CRUX_BENCH_FIRST_ACCOUNT_LENGTH_MIN ||
        length > CRUX_BENCH_FIRST_ACCOUNT_LENGTH_MAX) {
        return CRUX_ACCESS_ERR_PASSWORD_REJECTED;
    }
    /* Printable ASCII, so no space, no newline, no NUL and nothing above 0x7e. */
    for (size_t index = 0; index < length; ++index) {
        if (file[index] < 0x21u || file[index] > 0x7eu) {
            return CRUX_ACCESS_ERR_PASSWORD_REJECTED;
        }
    }
    return crux_access_provision_first_account(user, (const char *)file, length, role, &serial);
}
