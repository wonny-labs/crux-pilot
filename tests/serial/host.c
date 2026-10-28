#define _XOPEN_SOURCE 700 // NOLINT(bugprone-reserved-identifier): POSIX feature selector
#include "serial_linux.h"
#include "serial_port.h"
#include <assert.h>
#include <crux/serial.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

static const crux_serial_config_t config = {115200, 8, 1, CRUX_SERIAL_PARITY_NONE};
static int master, serial_fd = -1;
static const char *path;
static bool expire_wait, fail_close, hangup_after_write;
static atomic_bool read_admitted;
static crux_serial_t serial;
static void pause_ms(unsigned ms)
{
    struct timespec t = {(time_t)(ms / 1000), (long)(ms % 1000) * 1000000L};
    while (nanosleep(&t, &t) < 0 && errno == EINTR) {
    }
}
// GNU ld requires the __real_/__wrap_ names for these native fault probes.
// NOLINTBEGIN(bugprone-reserved-identifier)
int __real_open(const char *name, int flags, ...);
int __wrap_open(const char *name, int flags, ...)
{
    int fd = __real_open(name, flags);
    if (path != NULL && strcmp(path, name) == 0 && fd >= 0)
        serial_fd = fd;
    return fd;
}
int __real_close(int fd);
int __wrap_close(int fd)
{
    int result = __real_close(fd);
    if (fail_close && fd == serial_fd) {
        errno = EIO;
        return -1;
    }
    return result;
}
ssize_t __real_write(int fd, const void *data, size_t size);
ssize_t __wrap_write(int fd, const void *data, size_t size)
{
    ssize_t count = __real_write(fd, data, size);
    if (hangup_after_write && fd == serial_fd && count > 0) {
        hangup_after_write = false;
        assert(close(master) == 0);
    }
    return count;
}
int __real_poll(struct pollfd *fds, nfds_t count, int timeout);
int __wrap_poll(struct pollfd *fds, nfds_t count, int timeout)
{
    if (expire_wait && timeout > 0) {
        expire_wait = false;
        pause_ms(25);
        assert(write(master, "Z", 1) == 1);
        assert(count == 1);
        fds[0].revents = POLLIN;
        return 1;
    }
    return __real_poll(fds, count, timeout);
}
/* Admission witness belongs to test linking, not the public contract. */
crux_status_t __real_crux_serial_impl_read(uint32_t port, void *buffer, size_t size,
                                      const serial_deadline_t *deadline, size_t *count);
crux_status_t __wrap_crux_serial_impl_read(uint32_t port, void *buffer, size_t size,
                                      const serial_deadline_t *deadline, size_t *count)
{
    atomic_store(&read_admitted, true);
    return __real_crux_serial_impl_read(port, buffer, size, deadline, count);
}
static void setup(void)
{
    master = posix_openpt(O_RDWR | O_NOCTTY | O_NONBLOCK);
    assert(master >= 0 && grantpt(master) == 0 && unlockpt(master) == 0);
    path = ptsname(master);
    assert(path != NULL);
    assert(crux_serial_linux_bind(0, path) == CRUX_OK);
    assert(crux_serial_open(0, &config, &serial) == CRUX_OK);
}
static void cleanup(void)
{
    assert(crux_serial_close(&serial) == CRUX_OK && serial.slot == 0);
    assert(close(master) == 0);
}
// NOLINTEND(bugprone-reserved-identifier)
static void basic(void)
{
    setup();
    const unsigned char input[] = {0, 255, 10, 13, 127};
    assert(write(master, input, sizeof(input)) == (ssize_t)sizeof(input));
    unsigned char output[7];
    memset(output, 42, sizeof(output));
    size_t n = 0, done = 0;
    while (done < sizeof(input)) {
        assert(crux_serial_read(serial, output + done, sizeof(input) - done, 200, &n) == CRUX_OK);
        assert(n > 0);
        done += n;
    }
    assert(!memcmp(input, output, sizeof(input)) && output[5] == 42);
    unsigned char large[300];
    memset(large, 255, sizeof(large));
    assert(crux_serial_write(serial, large, sizeof(large), 200, &n) == CRUX_OK);
    assert(n > 0 && n < sizeof(large));
    unsigned char echoed[300];
    done = 0;
    while (done < n) {
        struct pollfd fd = {.fd = master, .events = POLLIN};
        assert(poll(&fd, 1, 1000) == 1);
        ssize_t got = read(master, echoed + done, n - done);
        assert(got > 0);
        done += (size_t)got;
    }
    assert(!memcmp(large, echoed, n));
    errno = 0;
    assert(open(path, O_RDWR | O_NOCTTY | O_NONBLOCK) < 0 && errno == EBUSY);
    crux_serial_t stale = serial;
    assert(crux_serial_close(&serial) == CRUX_OK);
    assert(crux_serial_open(0, &config, &serial) == CRUX_OK);
    assert(crux_serial_read(stale, output, 1, 0, &n) == CRUX_ERR_INVALID_STATE && n == 0);
    cleanup();
}
static void waiting(void)
{
    setup();
    unsigned char byte = 17;
    size_t n = 99;
    assert(crux_serial_read(serial, &byte, 1, 0, &n) == CRUX_ERR_BUSY && n == 0 && byte == 17);
    assert(crux_serial_read(serial, &byte, 1, 10, &n) == CRUX_ERR_TIMEOUT && n == 0 && byte == 17);
    expire_wait = true;
    assert(crux_serial_read(serial, &byte, 1, 5, &n) == CRUX_ERR_TIMEOUT && n == 0 && byte == 17);
    assert(crux_serial_read(serial, &byte, 1, 200, &n) == CRUX_OK && n == 1 && byte == 'Z');
    cleanup();
}
static void *reader(void *arg)
{
    (void)arg;
    unsigned char byte = 0;
    size_t n = 0;
    assert(crux_serial_read(serial, &byte, 1, 1000, &n) == CRUX_OK && n == 1 && byte == 'R');
    return NULL;
}
static void concurrent(void)
{
    setup();
    pthread_t thread;
    assert(pthread_create(&thread, NULL, reader, NULL) == 0);
    unsigned waited = 0;
    while (!atomic_load(&read_admitted) && waited++ < 2000)
        pause_ms(1);
    assert(atomic_load(&read_admitted));
    crux_serial_t alias = serial;
    assert(crux_serial_close(&alias) == CRUX_ERR_BUSY);
    unsigned char byte = 0;
    size_t n = 99;
    assert(crux_serial_read(serial, &byte, 1, 0, &n) == CRUX_ERR_BUSY && n == 0);
    assert(crux_serial_write(serial, "T", 1, 100, &n) == CRUX_OK && n == 1);
    struct pollfd fd = {.fd = master, .events = POLLIN};
    assert(poll(&fd, 1, 500) == 1 && read(master, &byte, 1) == 1 && byte == 'T');
    assert(write(master, "R", 1) == 1);
    assert(pthread_join(thread, NULL) == 0);
    cleanup();
}
static void hangup(void)
{
    setup();
    assert(close(master) == 0);
    unsigned char byte = 19;
    size_t n = 99;
    assert(crux_serial_read(serial, &byte, 1, 100, &n) == CRUX_ERR_IO && n == 0 && byte == 19);
    assert(crux_serial_write(serial, &byte, 1, 0, &n) == CRUX_ERR_IO && n == 0);
    /* A hangup can make native settings restoration fail; handle still retires. */
    crux_status_t result = crux_serial_close(&serial);
    assert((result == CRUX_OK || result == CRUX_ERR_IO) && serial.slot == 0);
}
static void cleanup_failure(void)
{
    setup();
    fail_close = true;
    assert(crux_serial_close(&serial) == CRUX_ERR_IO && serial.slot == 0);
    assert(crux_serial_open(0, &config, &serial) == CRUX_ERR_NOT_READY && serial.slot == 0);
    assert(close(master) == 0);
}
static void unavailable(void)
{
    assert(crux_serial_linux_bind(0, "/dev/crux-no-such-serial-test") == CRUX_OK);
    assert(crux_serial_open(0, &config, &serial) == CRUX_ERR_NOT_READY && serial.slot == 0);
}
static void admission(void)
{
    setup();
    assert(write(master, "A", 1) == 1);
    assert(serial_port_metadata_lock(NULL) == CRUX_OK);
    unsigned char byte = 17;
    size_t count = 99;
    assert(crux_serial_read(serial, &byte, 1, 0, &count) == CRUX_ERR_BUSY && count == 0);
    assert(crux_serial_read(serial, &byte, 1, 5, &count) == CRUX_ERR_TIMEOUT && count == 0);
    assert(byte == 17);
    serial_port_metadata_unlock();
    assert(crux_serial_read(serial, &byte, 1, 200, &count) == CRUX_OK && count == 1 && byte == 'A');
    cleanup();
}
static void committed_prefix(void)
{
    setup();
    hangup_after_write = true;
    size_t count = 0;
    assert(crux_serial_write(serial, "Q", 1, 100, &count) == CRUX_OK && count == 1);
    unsigned char byte = 31;
    assert(crux_serial_read(serial, &byte, 1, 0, &count) == CRUX_ERR_IO && count == 0 &&
           byte == 31);
    crux_status_t result = crux_serial_close(&serial);
    assert((result == CRUX_OK || result == CRUX_ERR_IO) && serial.slot == 0);
}
static void configuration(void)
{
    setup();
    assert(crux_serial_close(&serial) == CRUX_OK);
    crux_serial_config_t alternative = {9600, 8, 2, CRUX_SERIAL_PARITY_NONE};
    assert(crux_serial_open(0, &alternative, &serial) == CRUX_OK);
    struct termios actual;
    assert(tcgetattr(serial_fd, &actual) == 0);
    assert(cfgetispeed(&actual) == B9600 && cfgetospeed(&actual) == B9600);
    assert((actual.c_cflag & CSIZE) == CS8 && (actual.c_cflag & CSTOPB) != 0);
    assert((actual.c_lflag & (ICANON | ECHO)) == 0 && (actual.c_oflag & OPOST) == 0);
    cleanup();
}
int main(int argc, char **argv)
{
    assert(argc == 2);
    if (!strcmp(argv[1], "basic"))
        basic();
    else if (!strcmp(argv[1], "waiting"))
        waiting();
    else if (!strcmp(argv[1], "concurrent"))
        concurrent();
    else if (!strcmp(argv[1], "hangup"))
        hangup();
    else if (!strcmp(argv[1], "cleanup_failure"))
        cleanup_failure();
    else if (!strcmp(argv[1], "unavailable"))
        unavailable();
    else if (!strcmp(argv[1], "admission"))
        admission();
    else if (!strcmp(argv[1], "committed_prefix"))
        committed_prefix();
    else if (!strcmp(argv[1], "configuration"))
        configuration();
    else
        assert(false);
    return 0;
}
