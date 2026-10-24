#ifndef CRUX_SERIAL_HOST_H
#define CRUX_SERIAL_HOST_H
#include <crux/serial.h>
/* Trusted composition only. Bind an owned TTY once, before concurrent access.
 * The path is copied. No device is opened by bind. No public discovery API. */
crux_status_t crux_serial_host_bind(crux_serial_port_t port, const char *path);
#endif
