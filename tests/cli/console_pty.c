/* The console on the real Host Serial provider, over a pseudo-terminal: a login, a command and a
 * logout end to end, then a hang-up. This runs on a Linux TTY, which says nothing about a UART. */
#define _XOPEN_SOURCE 700 // NOLINT(bugprone-reserved-identifier): POSIX feature selector
#include "cli_console.h"
#include "cli_session_test.h"
#include "serial_linux.h"

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <pthread.h>
#include <stdlib.h>
#include <termios.h>
#include <unistd.h>

#define PASSWORD "correct-horse-password"

static int master = -1;
static char received[4096];
static size_t received_length;
static crux_cli_result_t cycle_result = CRUX_CLI_OK;

static const crux_cli_console_config_t config = {
    0, {115200, 8, 1, CRUX_SERIAL_PARITY_NONE}, "administrator"};

static void *console_thread(void *argument)
{
    (void)argument;
    crux_cli_console_state_t state = {{0, 0}, false};
    cycle_result = crux_cli_console_cycle(&state, &cli_test_context, &config);
    return NULL;
}

/* Reads from the master until this text has arrived, or fails after 10 seconds. */
static bool wait_for(const char *text)
{
    for (unsigned attempt = 0; attempt < 100u; ++attempt) {
        if (strstr(received, text) != NULL) {
            return true;
        }
        struct pollfd poll_master = {master, POLLIN, 0};
        if (poll(&poll_master, 1, 100) > 0 && (poll_master.revents & POLLIN) != 0) {
            ssize_t count =
                read(master, received + received_length, sizeof(received) - 1u - received_length);
            if (count > 0) {
                received_length += (size_t)count;
                received[received_length] = '\0';
            }
        }
    }
    printf("timed out waiting for [%s] in [%s]\n", text, received);
    return false;
}

static void send_text(const char *text)
{
    size_t length = strlen(text);
    CHECK(write(master, text, length) == (ssize_t)length);
}

int main(void)
{
    cli_test_begin();
    CHECK(crux_cli_init(NULL, NULL, 0) == CRUX_CLI_OK);

    master = posix_openpt(O_RDWR | O_NOCTTY);
    CHECK(master >= 0);
    CHECK(grantpt(master) == 0);
    CHECK(unlockpt(master) == 0);
    const char *path = ptsname(master);
    CHECK(path != NULL);
    CHECK(crux_serial_linux_bind(0, path) == CRUX_OK);

    pthread_t thread;
    CHECK(pthread_create(&thread, NULL, console_thread, NULL) == 0);

    CHECK(wait_for("login: "));
    send_text("alice\r");
    CHECK(wait_for("password: "));
    send_text(PASSWORD "\r");
    CHECK(wait_for("> "));
    CHECK(cli_fake_access.login_calls == 1);
    send_text("help logout\r");
    CHECK(wait_for("logout - end this session\r\n> "));
    send_text("logout\r");
    received_length = 0;
    received[0] = '\0';
    CHECK(wait_for("login: "));
    CHECK(cli_fake_access.logout_calls == 1);
    CHECK(strstr(received, PASSWORD) == NULL);

    /* A hang-up ends the cycle as a transport failure, with the port closed. */
    CHECK(close(master) == 0);
    CHECK(pthread_join(thread, NULL) == 0);
    CHECK(cycle_result == CRUX_CLI_ERR_OUTPUT);
    /* One for the read, and another if the provider cannot close cleanly after the hang-up. */
    CHECK(cli_fake_event_count_of(CRUX_CLI_EVENT_CONSOLE_ERROR) >= 1);
    return cli_test_finish("cli console on a pseudo-terminal");
}
