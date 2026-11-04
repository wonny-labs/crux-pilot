/* Until #192 replaces the stand-in, nobody can log in. #192 replaces this test with its own. */
#include "crux_ssh_attachment.h"
#include "crux_ssh_callbacks.h"
#include "ssh_check.h"

int main(void)
{
    const crux_ssh_peer_t peer = {.address = {192, 0, 2, 1}, .address_length = 4};
    const uint8_t secret[] = {'s'};
    crux_ssh_attachment_t *attachment = NULL;

    SSH_CHECK(crux_ssh_access_password(NULL, &peer, "user", secret, 1, &attachment) ==
              CRUX_SSH_AUTH_DENIED);
    SSH_CHECK(crux_ssh_access_key_check(NULL, &peer, "user", CRUX_SSH_KEY_ED25519, secret, 1,
                                        true) == CRUX_SSH_AUTH_DENIED);
    SSH_CHECK(crux_ssh_access_key_verified(NULL, &peer, "user", CRUX_SSH_KEY_ED25519, secret, 1,
                                           &attachment) == CRUX_SSH_AUTH_DENIED);
    SSH_CHECK(attachment == NULL);
    SSH_CHECK(!crux_ssh_access_session_alive(NULL, NULL));
    SSH_CHECK(crux_ssh_attachment_session(NULL).value == 0u);
    printf("%u failures\n", ssh_check_failures);
    return ssh_check_failures == 0 ? 0 : 1;
}
