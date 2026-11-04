/* Until #193 replaces the stand-in, every session fails. #193 replaces this test with its own. */
#include "crux_ssh_callbacks.h"
#include "ssh_check.h"

int main(void)
{
    SSH_CHECK(crux_ssh_cli_shell(NULL, NULL, NULL, false) == 255);
    SSH_CHECK(crux_ssh_cli_exec(NULL, NULL, NULL, "status", 6) == 255);
    printf("%u failures\n", ssh_check_failures);
    return ssh_check_failures == 0 ? 0 : 1;
}
