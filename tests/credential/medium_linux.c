#include "medium_linux.h"

#include "credential_test.h"
#include "file_linux.h"

#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>

static char base[] = "/tmp/crux-credential-XXXXXX";
static char root_paths[2][64];

void credential_medium_blank(void)
{
    test_slot_remove(TEST_SLOT_A);
    test_slot_remove(TEST_SLOT_B);
}

void credential_medium_linux_open(void)
{
    if (mkdtemp(base) == NULL) {
        abort();
    }
    for (size_t index = 0; index < 2u; ++index) {
        int written = snprintf(root_paths[index], sizeof(root_paths[index]), "%s/secure-%c", base,
                               index == 0 ? 'a' : 'b');
        if (written <= 0 || (size_t)written >= sizeof(root_paths[index]) ||
            mkdir(root_paths[index], 0700) != 0) {
            abort();
        }
    }
    if (crux_file_linux_bind_root(TEST_ROOT_A, root_paths[0]) != CRUX_FILE_OK ||
        crux_file_linux_bind_root(TEST_ROOT_B, root_paths[1]) != CRUX_FILE_OK) {
        abort();
    }
}

void credential_medium_linux_close(void)
{
    credential_medium_blank();
    if (rmdir(root_paths[0]) != 0 || rmdir(root_paths[1]) != 0 || rmdir(base) != 0) {
        abort();
    }
}
