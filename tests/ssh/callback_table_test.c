/* Every member of the shared table is set, and a provider accepts it. */
#include "crux_ssh_callbacks.h"
#include "fake_provider.h"
#include "ssh_check.h"

int main(void)
{
    const crux_ssh_test_client_t *client = crux_ssh_fake_client();
    const crux_ssh_callbacks_t *table = crux_ssh_callback_table();
    crux_ssh_config_t config = {
        .max_connections = 1,
        .max_auth_attempts = 3,
        .auth_timeout_ms = 1,
        .io_timeout_ms = 1,
        .host_key_type = client->host_key_type,
        .host_key = client->host_key,
        .host_key_length = client->host_key_length,
    };

    crux_ssh_fake_reset();
    SSH_CHECK(table->password == crux_ssh_access_password);
    SSH_CHECK(table->key_check == crux_ssh_access_key_check);
    SSH_CHECK(table->key_verified == crux_ssh_access_key_verified);
    SSH_CHECK(table->key_failed == crux_ssh_access_key_failed);
    SSH_CHECK(table->shell == crux_ssh_cli_shell);
    SSH_CHECK(table->exec == crux_ssh_cli_exec);
    SSH_CHECK(table->sftp_open == crux_ssh_sftp_open);
    SSH_CHECK(table->sftp_close == crux_ssh_sftp_close);
    SSH_CHECK(table->session_alive == crux_ssh_access_session_alive);
    SSH_CHECK(table->connection_closed == crux_ssh_access_connection_closed);
    SSH_CHECK(client->provider->start(&config, table) == CRUX_OK);
    SSH_CHECK(client->provider->stop() == CRUX_OK);
    printf("%u failures\n", ssh_check_failures);
    return ssh_check_failures == 0 ? 0 : 1;
}
