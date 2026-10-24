#ifndef CRUX_LOGGING_CONTROLLER_H
#define CRUX_LOGGING_CONTROLLER_H
#include <crux/logging.h>
#include <stdbool.h>
/* Emits one state record. OK is not a delivery acknowledgement. */
crux_status_t logging_controller_state(bool healthy);
#endif
