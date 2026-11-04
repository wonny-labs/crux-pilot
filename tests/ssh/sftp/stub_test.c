/* Until #194 replaces the stand-in, SFTP is refused. #194 replaces this test with its own. */
#include "crux_ssh_callbacks.h"
#include "ssh_check.h"

int main(void)
{
    crux_ssh_file_handle_t *handle = NULL;

    SSH_CHECK(crux_ssh_sftp_open(NULL, NULL, &handle) == CRUX_ERR_NOT_SUPPORTED);
    SSH_CHECK(handle == NULL);
    printf("%u failures\n", ssh_check_failures);
    return ssh_check_failures == 0 ? 0 : 1;
}
