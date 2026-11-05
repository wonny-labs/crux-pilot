#include "fake_transport.h"

#include <stdint.h>
#include <string.h>

void cli_fake_reset(cli_fake_t *fake)
{
    memset(fake, 0, sizeof(*fake));
    fake->write_fail_after = SIZE_MAX;
    fake->hook_position = SIZE_MAX;
}

void cli_fake_input_bytes(cli_fake_t *fake, const void *data, size_t length)
{
    if (length > CLI_FAKE_INPUT_MAX - fake->input_length) {
        length = CLI_FAKE_INPUT_MAX - fake->input_length;
    }
    memcpy(fake->input + fake->input_length, data, length);
    fake->input_length += length;
}

void cli_fake_input(cli_fake_t *fake, const char *text)
{
    cli_fake_input_bytes(fake, text, strlen(text));
}

void cli_fake_input_timeouts(cli_fake_t *fake, unsigned count)
{
    if (fake->timeout_count < CLI_FAKE_TIMEOUTS_MAX) {
        fake->timeouts[fake->timeout_count].position = fake->input_length;
        fake->timeouts[fake->timeout_count].count = count;
        fake->timeout_count += 1;
    }
}

void cli_fake_output_clear(cli_fake_t *fake)
{
    fake->output_length = 0;
    fake->output[0] = '\0';
}

crux_cli_io_t cli_fake_io(cli_fake_t *fake, bool echo)
{
    crux_cli_io_t io = {cli_fake_read, cli_fake_write, fake, echo};
    return io;
}

static bool scripted_timeout(cli_fake_t *fake)
{
    for (size_t index = 0; index < fake->timeout_count; ++index) {
        if (fake->timeouts[index].position == fake->input_position &&
            fake->timeouts[index].count > 0) {
            fake->timeouts[index].count -= 1;
            return true;
        }
    }
    return false;
}

static int report_read_timeout(cli_fake_t *fake)
{
    fake->read_timeouts_seen += 1;
    if (fake->on_read_timeout != NULL) {
        fake->on_read_timeout(fake->hook_context);
    }
    return 0;
}

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters): the contract fixes this signature.
int cli_fake_read(void *transport, void *buffer, size_t length, uint32_t timeout_ms)
{
    cli_fake_t *fake = transport;
    (void)timeout_ms;
    fake->read_calls += 1;
    if (scripted_timeout(fake)) {
        return report_read_timeout(fake);
    }
    if (fake->input_position >= fake->input_length) {
        if (fake->wait_when_empty && fake->empty_timeouts_left > 0) {
            fake->empty_timeouts_left -= 1;
            return report_read_timeout(fake);
        }
        return -1;
    }
    if (fake->read_overclaims) {
        return (int)length + 1;
    }
    if (fake->input_position == fake->hook_position && fake->on_position != NULL) {
        cli_fake_hook_t hook = fake->on_position;
        fake->on_position = NULL;
        hook(fake->hook_context);
    }
    size_t available = fake->input_length - fake->input_position;
    size_t count = length < available ? length : available;
    memcpy(buffer, fake->input + fake->input_position, count);
    fake->input_position += count;
    return (int)count;
}

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters): the contract fixes this signature.
int cli_fake_write(void *transport, const void *buffer, size_t length, uint32_t timeout_ms)
{
    cli_fake_t *fake = transport;
    (void)timeout_ms;
    fake->write_calls += 1;
    if (fake->write_failed || fake->output_length >= fake->write_fail_after) {
        return -1;
    }
    if (fake->write_stuck || fake->write_timeouts > 0) {
        if (fake->write_timeouts > 0) {
            fake->write_timeouts -= 1;
        }
        fake->write_timeouts_seen += 1;
        if (fake->on_write_timeout != NULL) {
            fake->on_write_timeout(fake->hook_context);
        }
        return 0;
    }
    if (fake->write_overclaims) {
        return (int)length + 1;
    }
    size_t count = length;
    if (fake->write_limit != 0 && count > fake->write_limit) {
        count = fake->write_limit;
    }
    if (fake->write_fail_after != SIZE_MAX &&
        count > fake->write_fail_after - fake->output_length) {
        count = fake->write_fail_after - fake->output_length;
    }
    if (count > CLI_FAKE_OUTPUT_MAX - 1u - fake->output_length) {
        count = CLI_FAKE_OUTPUT_MAX - 1u - fake->output_length;
    }
    /* A full capture is a test's mistake. Failing here cannot turn into an endless retry. */
    if (count == 0 && length > 0) {
        return -1;
    }
    memcpy(fake->output + fake->output_length, buffer, count);
    fake->output_length += count;
    fake->output[fake->output_length] = '\0';
    return (int)count;
}
