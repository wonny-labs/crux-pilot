#ifndef SERIAL_CONTROLLER_H
#define SERIAL_CONTROLLER_H
#include <crux/serial.h>
/* One binary request/acknowledgement. Success requires a matching peer response;
 * Caller supplies a fresh nonce, including after reopen.
 * Queued writes alone never count as completion. No retransmission policy. */
crux_status_t serial_controller_run(crux_serial_port_t port, uint32_t nonce);
#endif
