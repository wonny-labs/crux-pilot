#include "credential_test.h"
#include "fake_file.h"

#include <stdlib.h>

void credential_medium_blank(void)
{
    crux_file_fake_reset();
    if (crux_file_fake_bind_root(TEST_ROOT_A, "/card/secure-a") != CRUX_FILE_OK ||
        crux_file_fake_bind_root(TEST_ROOT_B, "/card/secure-b") != CRUX_FILE_OK) {
        abort();
    }
}
