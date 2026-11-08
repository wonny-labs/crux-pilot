/* A slow client holds only its own session, on real threads and the real access core. The provider
 * here is a double with a bounded wait, shaped as the contract says: a thread per connection, and a
 * write with no room gives CRUX_ERR_TIMEOUT. It says nothing about a real SSH library. */
#include "access_test.h"
#include "crux_cli_adapter.h"
#include "crux_ssh_callbacks.h"
#include "crux_ssh_cli.h"
#include "fake_attachment.h"

#include <pthread.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <time.h>

#define FLOOD_LINES 40u
#define FLOOD_LINE "0123456789abcdefghijklmnopqrstuv\n"
#define FLOOD_BYTES (FLOOD_LINES * (sizeof(FLOOD_LINE) - 1u))
#define OTHER_ROUNDS 30u
/* One account holds three sessions at most, and the oldest then ends. */
#define WORKERS 3u
#define WORKER_ROUNDS 25u
#define WAIT_LIMIT_S 60

void crux_cli_test_reset(void);

struct crux_ssh_channel {
    const char *input;
    size_t input_length;
    size_t input_position;
    size_t write_limit;
    atomic_bool stuck;
    atomic_uint timeouts;
    size_t output_length;
    char output[8192];
    bool closed;
};

static void pause_briefly(void)
{
    const struct timespec delay = {0, 1000000};
    (void)nanosleep(&delay, NULL);
}

static crux_status_t double_read(crux_ssh_channel_t *channel, void *buffer, size_t length,
                                 size_t *transferred)
{
    (void)length;
    *transferred = 0;
    if (channel->closed) {
        return CRUX_ERR_INVALID_STATE;
    }
    if (channel->input_position == channel->input_length) {
        return CRUX_ERR_NO_DATA;
    }
    *(char *)buffer = channel->input[channel->input_position];
    channel->input_position += 1;
    *transferred = 1;
    return CRUX_OK;
}

static crux_status_t double_write(crux_ssh_channel_t *channel, const void *buffer, size_t length,
                                  size_t *transferred)
{
    *transferred = 0;
    if (channel->closed) {
        return CRUX_ERR_INVALID_STATE;
    }
    if (atomic_load(&channel->stuck)) {
        pause_briefly();
        atomic_fetch_add(&channel->timeouts, 1);
        return CRUX_ERR_TIMEOUT;
    }
    if (channel->write_limit != 0 && length > channel->write_limit) {
        length = channel->write_limit;
    }
    if (length > sizeof(channel->output) - channel->output_length) {
        return CRUX_ERR_IO;
    }
    memcpy(channel->output + channel->output_length, buffer, length);
    channel->output_length += length;
    *transferred = length;
    return CRUX_OK;
}

static crux_status_t double_close(crux_ssh_channel_t *channel)
{
    channel->closed = true;
    return CRUX_OK;
}

static const crux_ssh_provider_t provider = {
    .read = double_read, .write = double_write, .close = double_close};

static crux_cli_result_t run_flood(crux_cli_call_t *call, size_t argc, const char *const *argv,
                                   void *context)
{
    (void)argc;
    (void)argv;
    (void)context;
    for (unsigned line = 0; line < FLOOD_LINES; ++line) {
        crux_cli_result_t result = crux_cli_write(call, FLOOD_LINE, sizeof(FLOOD_LINE) - 1u);
        if (result != CRUX_CLI_OK) {
            return result;
        }
    }
    return CRUX_CLI_OK;
}

static const crux_cli_command_t commands[] = {
    CRUX_CLI_COMMAND("flood", "much output", CRUX_CLI_SESSION_ONLY, 0, 0, CRUX_CLI_CHANNEL_SSH,
                     run_flood, NULL),
};

typedef struct {
    crux_ssh_attachment_t *attachment;
    crux_ssh_channel_t *channel;
    bool terminal;
    bool exec;
    int status;
    atomic_bool finished;
} session_t;

static void *run_session(void *argument)
{
    session_t *session = argument;
    session->status =
        session->exec
            ? crux_ssh_cli_exec(NULL, session->attachment, session->channel, "flood", 5)
            : crux_ssh_cli_shell(NULL, session->attachment, session->channel, session->terminal);
    atomic_store(&session->finished, true);
    return NULL;
}

/* Up to the limit. Past it the session is stuck for good, and a join would hang, so the test ends.
 */
static void wait_for(atomic_bool *flag, int line)
{
    const time_t limit = time(NULL) + WAIT_LIMIT_S;
    while (!atomic_load(flag)) {
        if (time(NULL) > limit) {
            printf("FAIL %s:%d: a session did not finish\n", __FILE__, line);
            exit(1);
        }
        pause_briefly();
    }
}

static void begin(void)
{
    static const crux_cli_command_t *const tables[] = {commands};
    static const size_t counts[] = {1};
    crux_cli_test_reset();
    access_test_boot_with_admin();
    CHECK(crux_cli_init(tables, counts, 1) == CRUX_CLI_OK);
    crux_ssh_fake_attachment_reset();
    CHECK(crux_ssh_cli_set_provider(&provider) == CRUX_OK);
}

static crux_ssh_attachment_t *new_attachment(void)
{
    crux_ssh_attachment_t *attachment = crux_ssh_fake_attachment(
        access_test_session(ACCESS_TEST_ADMIN, ACCESS_TEST_PASSWORD, &access_test_ssh));
    CHECK(attachment != NULL);
    return attachment;
}

static void expect_flood(const crux_ssh_channel_t *channel)
{
    CHECK(channel->output_length == FLOOD_BYTES);
    for (size_t offset = 0; offset + sizeof(FLOOD_LINE) - 1u <= channel->output_length;
         offset += sizeof(FLOOD_LINE) - 1u) {
        CHECK(memcmp(channel->output + offset, FLOOD_LINE, sizeof(FLOOD_LINE) - 1u) == 0);
    }
}

/* Whole sessions of other clients run while one client's output is stuck. */
static void others_finish_while_one_is_stuck(bool stuck_in_exec)
{
    static crux_ssh_channel_t stuck_channel;
    static session_t stuck;
    static crux_ssh_channel_t other_channel;
    static session_t other;
    pthread_t stuck_thread;
    pthread_t other_thread;

    begin();
    memset(&stuck_channel, 0, sizeof(stuck_channel));
    memset(&stuck, 0, sizeof(stuck));
    atomic_store(&stuck_channel.stuck, true);
    stuck_channel.input = "flood\n";
    stuck_channel.input_length = stuck_in_exec ? 0 : 6;
    stuck.attachment = new_attachment();
    stuck.channel = &stuck_channel;
    stuck.exec = stuck_in_exec;
    crux_ssh_attachment_t *other_attachment = new_attachment();
    CHECK(pthread_create(&stuck_thread, NULL, run_session, &stuck) == 0);

    /* It is in its wait, the prompt or the output. */
    const time_t limit = time(NULL) + WAIT_LIMIT_S;
    while (atomic_load(&stuck_channel.timeouts) < 3u && time(NULL) <= limit) {
        pause_briefly();
    }
    CHECK(atomic_load(&stuck_channel.timeouts) >= 3u);

    for (unsigned round = 0; round < OTHER_ROUNDS; ++round) {
        memset(&other_channel, 0, sizeof(other_channel));
        memset(&other, 0, sizeof(other));
        other.attachment = other_attachment;
        other.channel = &other_channel;
        other.exec = true;
        CHECK(pthread_create(&other_thread, NULL, run_session, &other) == 0);
        wait_for(&other.finished, __LINE__);
        CHECK(pthread_join(other_thread, NULL) == 0);
        CHECK(other.status == 0);
        expect_flood(&other_channel);
        CHECK(!atomic_load(&stuck.finished));
    }

    /* The client catches up, and its own output is complete. */
    unsigned waited = atomic_load(&stuck_channel.timeouts);
    CHECK(waited >= 3u);
    atomic_store(&stuck_channel.stuck, false);
    wait_for(&stuck.finished, __LINE__);
    CHECK(pthread_join(stuck_thread, NULL) == 0);
    if (stuck_in_exec) {
        CHECK(stuck.status == 0);
        expect_flood(&stuck_channel);
    } else {
        CHECK(stuck.status == 1);
        CHECK(stuck_channel.output_length == 2u + FLOOD_BYTES + 2u);
        CHECK(memcmp(stuck_channel.output, "> ", 2) == 0);
    }
}

typedef struct {
    crux_ssh_attachment_t *attachment;
    unsigned failures;
} worker_t;

/* Short writes on a terminal keep a place in their line ends, and each session has its own. */
static void *run_worker(void *argument)
{
    static const char expected_prefix[] = "> flood\r\n";
    worker_t *worker = argument;
    for (unsigned round = 0; round < WORKER_ROUNDS; ++round) {
        crux_ssh_channel_t channel;
        memset(&channel, 0, sizeof(channel));
        channel.input = "flood\rlogout\r";
        channel.input_length = 13;
        channel.write_limit = 1;
        int status = crux_ssh_cli_shell(NULL, worker->attachment, &channel, true);
        if (status != 0) {
            printf("round %u: status %d\n", round, status);
            worker->failures += 1;
            continue;
        }
        const size_t expected = (sizeof(expected_prefix) - 1u) + FLOOD_LINES * sizeof(FLOOD_LINE) +
                                (sizeof("> logout\r\n") - 1u);
        if (channel.output_length != expected ||
            memcmp(channel.output, expected_prefix, sizeof(expected_prefix) - 1u) != 0) {
            printf("round %u: %zu bytes of %zu\n", round, channel.output_length, expected);
            worker->failures += 1;
        }
        for (size_t offset = 0; offset < channel.output_length; ++offset) {
            bool line_end = channel.output[offset] == '\n';
            if (line_end && (offset == 0 || channel.output[offset - 1u] != '\r')) {
                printf("round %u: LF with no CR at %zu\n", round, offset);
                worker->failures += 1;
            }
            if (channel.output[offset] == '\r' &&
                (offset + 1u == channel.output_length || channel.output[offset + 1u] != '\n')) {
                printf("round %u: CR with no LF at %zu\n", round, offset);
                worker->failures += 1;
            }
        }
    }
    return NULL;
}

static void sessions_side_by_side(void)
{
    static worker_t workers[WORKERS];
    pthread_t threads[WORKERS];

    begin();
    for (unsigned index = 0; index < WORKERS; ++index) {
        workers[index].attachment = new_attachment();
        workers[index].failures = 0;
    }
    for (unsigned index = 0; index < WORKERS; ++index) {
        CHECK(pthread_create(&threads[index], NULL, run_worker, &workers[index]) == 0);
    }
    for (unsigned index = 0; index < WORKERS; ++index) {
        CHECK(pthread_join(threads[index], NULL) == 0);
        CHECK(workers[index].failures == 0);
    }
}

int main(void)
{
    others_finish_while_one_is_stuck(true);
    others_finish_while_one_is_stuck(false);
    sessions_side_by_side();
    return access_test_finish("cli over SSH, side by side on real threads");
}
