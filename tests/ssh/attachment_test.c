/* The accessor's contract, on the Host double. */
#include "fake_attachment.h"
#include "ssh_check.h"

int main(void)
{
    const crux_access_session_t first = {41};
    const crux_access_session_t second = {42};
    crux_ssh_attachment_t *a;
    crux_ssh_attachment_t *b;

    crux_ssh_fake_attachment_reset();
    a = crux_ssh_fake_attachment(first);
    b = crux_ssh_fake_attachment(second);
    SSH_CHECK(a != NULL && b != NULL && a != b);
    SSH_CHECK(crux_ssh_attachment_session(a).value == first.value);
    SSH_CHECK(crux_ssh_attachment_session(b).value == second.value);
    /* Zero is never a valid access session. */
    SSH_CHECK(crux_ssh_attachment_session(NULL).value == 0u);
    printf("%u failures\n", ssh_check_failures);
    return ssh_check_failures == 0 ? 0 : 1;
}
