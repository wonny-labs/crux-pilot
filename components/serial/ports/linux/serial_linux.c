#include "serial_linux.h"
#include "serial_port.h"
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <poll.h>
#include <pthread.h>
#include <stdatomic.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

typedef struct {
    char path[256];
    int fd;
    bool bound, quarantine;
    atomic_bool failed;
    struct stat identity;
    struct termios saved;
} host_port_t;

static host_port_t ports[CRUX_SERIAL_PORT_COUNT];
static pthread_mutex_t metadata = PTHREAD_MUTEX_INITIALIZER;
/* Serializes native ownership acquisition/teardown only, never transfer waits. */
static pthread_mutex_t ownership = PTHREAD_MUTEX_INITIALIZER;

bool serial_port_context_allowed(void)
{
    return true;
}

crux_status_t serial_port_now(uint64_t *now)
{
    struct timespec value;
    if (clock_gettime(CLOCK_MONOTONIC, &value) != 0 || value.tv_sec < 0)
        return CRUX_ERR_IO;
    *now = (uint64_t)value.tv_sec * 1000 + (uint64_t)value.tv_nsec / 1000000;
    return CRUX_OK;
}

crux_status_t serial_port_metadata_lock(const serial_deadline_t *deadline)
{
    if (deadline == NULL) {
        return pthread_mutex_lock(&metadata) == 0 ? CRUX_OK : CRUX_ERR_IO;
    }
    for (;;) {
        int result = pthread_mutex_trylock(&metadata);
        if (result == 0)
            return CRUX_OK;
        if (result != EBUSY)
            return CRUX_ERR_IO;
        if (deadline->poll)
            return CRUX_ERR_BUSY;
        uint64_t now;
        if (serial_port_now(&now) != CRUX_OK)
            return CRUX_ERR_IO;
        if (now >= deadline->end_ms)
            return CRUX_ERR_TIMEOUT;
        struct timespec delay = {0, 1000000};
        (void)nanosleep(&delay, NULL);
    }
}

void serial_port_metadata_unlock(void)
{
    (void)pthread_mutex_unlock(&metadata);
}

crux_status_t crux_serial_linux_bind(crux_serial_port_t port, const char *path)
{
    if (port >= CRUX_SERIAL_PORT_COUNT || path == NULL || path[0] == '\0')
        return CRUX_ERR_INVALID_ARGUMENT;
    size_t size = strnlen(path, sizeof(ports[port].path));
    if (size == sizeof(ports[port].path))
        return CRUX_ERR_OUT_OF_RANGE;
    (void)pthread_mutex_lock(&ownership);
    crux_status_t result = CRUX_ERR_INVALID_STATE;
    if (!ports[port].bound) {
        memcpy(ports[port].path, path, size + 1);
        ports[port].fd = -1;
        ports[port].bound = true;
        result = CRUX_OK;
    }
    (void)pthread_mutex_unlock(&ownership);
    return result;
}

static speed_t speed(uint32_t baud)
{
    switch (baud) {
    case 9600:
        return B9600;
    case 19200:
        return B19200;
    case 38400:
        return B38400;
    case 57600:
        return B57600;
    default:
        return B115200;
    }
}

crux_status_t serial_port_open(uint32_t index, const crux_serial_config_t *config)
{
    host_port_t *port = &ports[index];
    (void)pthread_mutex_lock(&ownership);
    crux_status_t result = CRUX_OK;
    int fd = -1;
    bool claimed = false, saved = false, changed = false;
    if (!port->bound) {
        result = CRUX_ERR_INVALID_ARGUMENT;
        goto done;
    }
    if (port->quarantine) {
        result = CRUX_ERR_NOT_READY;
        goto done;
    }
    fd = open(port->path, O_RDWR | O_NOCTTY | O_NONBLOCK | O_CLOEXEC);
    if (fd < 0) {
        result = errno == EBUSY                                           ? CRUX_ERR_BUSY
                 : (errno == ENOENT || errno == ENODEV || errno == ENXIO) ? CRUX_ERR_NOT_READY
                                                                          : CRUX_ERR_IO;
        goto done;
    }
    if (!isatty(fd)) {
        result = CRUX_ERR_NOT_SUPPORTED;
        goto done;
    }
    struct stat identity;
    if (fstat(fd, &identity) != 0) {
        result = CRUX_ERR_IO;
        goto done;
    }
    for (uint32_t i = 0; i < CRUX_SERIAL_PORT_COUNT; ++i) {
        if (i != index && ports[i].bound && ports[i].fd >= 0 &&
            ports[i].identity.st_rdev == identity.st_rdev &&
            ports[i].identity.st_dev == identity.st_dev &&
            ports[i].identity.st_ino == identity.st_ino) {
            result = CRUX_ERR_BUSY;
            goto done;
        }
    }
    if (ioctl(fd, TIOCEXCL) != 0) {
        result = errno == EBUSY ? CRUX_ERR_BUSY : CRUX_ERR_IO;
        goto done;
    }
    claimed = true;
    if (tcgetattr(fd, &port->saved) != 0) {
        result = CRUX_ERR_IO;
        goto done;
    }
    saved = true;
    struct termios value = port->saved;
    value.c_iflag &= ~(IGNBRK | BRKINT | PARMRK | ISTRIP | INLCR | IGNCR | ICRNL | IXON | IXOFF |
                       IXANY | INPCK | IGNPAR);
    value.c_oflag &= ~OPOST;
    value.c_lflag &= ~(ECHO | ECHONL | ICANON | ISIG | IEXTEN);
    value.c_cflag &= ~(CSIZE | PARENB | PARODD | CSTOPB | CRTSCTS);
    value.c_cflag |= CREAD | CLOCAL | (config->data_bits == 7 ? CS7 : CS8);
    if (config->parity != CRUX_SERIAL_PARITY_NONE)
        value.c_cflag |= PARENB;
    if (config->parity == CRUX_SERIAL_PARITY_ODD)
        value.c_cflag |= PARODD;
    if (config->stop_bits == 2)
        value.c_cflag |= CSTOPB;
    value.c_cc[VMIN] = 0;
    value.c_cc[VTIME] = 0;
    if (cfsetispeed(&value, speed(config->baud_rate)) != 0 ||
        cfsetospeed(&value, speed(config->baud_rate)) != 0 || tcsetattr(fd, TCSANOW, &value) != 0) {
        result = errno == EINVAL ? CRUX_ERR_NOT_SUPPORTED : CRUX_ERR_IO;
        goto done;
    }
    changed = true;
    struct termios actual;
    if (tcgetattr(fd, &actual) != 0) {
        result = CRUX_ERR_IO;
        goto done;
    }
    tcflag_t mask = CSIZE | PARENB | PARODD | CSTOPB | CRTSCTS;
    if ((actual.c_cflag & mask) != (value.c_cflag & mask) ||
        cfgetispeed(&actual) != speed(config->baud_rate) ||
        cfgetospeed(&actual) != speed(config->baud_rate)) {
        result = CRUX_ERR_NOT_SUPPORTED;
        goto done;
    }
    port->fd = fd;
    port->identity = identity;
    atomic_store(&port->failed, false);
    fd = -1;
done:
    if (fd >= 0) {
        bool unsafe = false;
        if (changed && saved && tcsetattr(fd, TCSANOW, &port->saved) != 0)
            unsafe = true;
        if (claimed && ioctl(fd, TIOCNXCL) != 0)
            unsafe = true;
        if (close(fd) != 0)
            unsafe = true;
        if (unsafe) {
            port->quarantine = true;
            result = CRUX_ERR_IO;
        }
    }
    (void)pthread_mutex_unlock(&ownership);
    return result;
}

crux_status_t serial_port_close(uint32_t index)
{
    host_port_t *port = &ports[index];
    (void)pthread_mutex_lock(&ownership);
    bool unsafe = false;
    if (tcflush(port->fd, TCIOFLUSH) != 0)
        unsafe = true;
    if (tcsetattr(port->fd, TCSANOW, &port->saved) != 0)
        unsafe = true;
    if (ioctl(port->fd, TIOCNXCL) != 0)
        unsafe = true;
    if (close(port->fd) != 0)
        unsafe = true;
    port->fd = -1;
    port->quarantine = unsafe;
    (void)pthread_mutex_unlock(&ownership);
    return unsafe ? CRUX_ERR_IO : CRUX_OK;
}

static crux_status_t native_transfer(uint32_t index, void *destination, size_t length,
                                     const void *source, const serial_deadline_t *deadline,
                                     size_t *count, bool writing)
{
    host_port_t *port = &ports[index];
    unsigned char staging[256];
    size_t size = length < sizeof(staging) ? length : sizeof(staging);
    for (;;) {
        if (atomic_load(&port->failed))
            return CRUX_ERR_IO;
        uint64_t now;
        if (serial_port_now(&now) != CRUX_OK)
            return CRUX_ERR_IO;
        if (!deadline->poll && now >= deadline->end_ms)
            return CRUX_ERR_TIMEOUT;
        struct pollfd fd = {.fd = port->fd, .events = writing ? POLLOUT : POLLIN};
        int remaining = deadline->poll ? 0 : (int)(deadline->end_ms - now);
        int ready = poll(&fd, 1, remaining);
        if (ready < 0 && errno == EINTR)
            continue;
        if (ready < 0 || (fd.revents & (POLLHUP | POLLERR | POLLNVAL))) {
            atomic_store(&port->failed, true);
            return CRUX_ERR_IO;
        }
        if (ready == 0)
            return deadline->poll ? CRUX_ERR_BUSY : CRUX_ERR_TIMEOUT;
        if (serial_port_now(&now) != CRUX_OK)
            return CRUX_ERR_IO;
        if (!deadline->poll && now >= deadline->end_ms)
            return CRUX_ERR_TIMEOUT;
        if (atomic_load(&port->failed))
            return CRUX_ERR_IO;
        ssize_t amount = writing ? write(port->fd, source, size) : read(port->fd, staging, size);
        if (amount > 0) {
            if (!writing)
                memcpy(destination, staging, (size_t)amount);
            *count = (size_t)amount;
            return CRUX_OK;
        }
        if (amount < 0 && errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR) {
            atomic_store(&port->failed, true);
            return CRUX_ERR_IO;
        }
        if (deadline->poll)
            return CRUX_ERR_BUSY;
    }
}

crux_status_t serial_port_read(uint32_t port, void *buffer, size_t size,
                               const serial_deadline_t *deadline, size_t *count)
{
    return native_transfer(port, buffer, size, NULL, deadline, count, false);
}

crux_status_t serial_port_write(uint32_t port, const void *buffer, size_t size,
                                const serial_deadline_t *deadline, size_t *count)
{
    return native_transfer(port, NULL, size, buffer, deadline, count, true);
}
