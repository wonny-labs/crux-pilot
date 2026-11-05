/* An image with no file service refuses SFTP, in the entry point and in every operation. */
#include "crux_ssh_callbacks.h"
#include "crux_ssh_sftp_ops.h"
#include "ssh_check.h"

int main(void)
{
    crux_ssh_file_handle_t *handle = NULL;
    char session[1];
    char buffer[CRUX_SSH_SFTP_NAME_CAPACITY];
    const uint32_t offset[2] = {0, 0};
    crux_ssh_sftp_attributes_t attributes;
    int dir = 0;

    SSH_CHECK(crux_ssh_sftp_open(NULL, NULL, &handle) == CRUX_ERR_NOT_SUPPORTED);
    SSH_CHECK(handle == NULL);
    crux_ssh_sftp_close(NULL, NULL, NULL);
    SSH_CHECK(crux_ssh_sftp_file_open(session, "/a/b", CRUX_SSH_SFTP_O_RDONLY) < 0);
    SSH_CHECK(crux_ssh_sftp_file_close(session, 0) < 0);
    SSH_CHECK(crux_ssh_sftp_file_pread(session, 0, buffer, 1u, offset) < 0);
    SSH_CHECK(crux_ssh_sftp_file_pwrite(session, 0, buffer, 1u, offset) < 0);
    SSH_CHECK(crux_ssh_sftp_file_append(session, 0, buffer, 1u) < 0);
    SSH_CHECK(crux_ssh_sftp_file_attributes(session, 0, &attributes) < 0);
    SSH_CHECK(crux_ssh_sftp_file_set_attributes(session, 0) < 0);
    SSH_CHECK(crux_ssh_sftp_remove(session, "/a/b") < 0);
    SSH_CHECK(crux_ssh_sftp_rename(session, "/a/b", "/a/c") < 0);
    SSH_CHECK(crux_ssh_sftp_attributes(session, "/a/b", &attributes) < 0);
    SSH_CHECK(crux_ssh_sftp_set_attributes(session, "/a/b") < 0);
    SSH_CHECK(crux_ssh_sftp_directory_refused(session, "/a") == 0);
    SSH_CHECK(crux_ssh_sftp_dir_open(session, "/a", &dir) < 0);
    SSH_CHECK(crux_ssh_sftp_dir_read(session, 0, buffer, sizeof(buffer), &attributes) < 0);
    SSH_CHECK(crux_ssh_sftp_dir_close(session, 0) < 0);
    SSH_CHECK(crux_ssh_sftp_start_directory(buffer, sizeof(buffer)) == NULL);
    printf("%u failures\n", ssh_check_failures);
    return ssh_check_failures == 0 ? 0 : 1;
}
