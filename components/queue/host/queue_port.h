#ifndef CRUX_QUEUE_PORT_H
#define CRUX_QUEUE_PORT_H
#include <crux/status.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <time.h>
typedef struct {
    struct timespec absolute;
    bool poll;
} queue_deadline_t;
bool queue_port_context(void);
crux_status_t queue_port_deadline(uint32_t ms, queue_deadline_t *out);
crux_status_t queue_port_lock(const queue_deadline_t *deadline);
void queue_port_unlock(void);
crux_status_t queue_port_prepare(size_t index);
crux_status_t queue_port_wait(size_t index, const queue_deadline_t *deadline);
void queue_port_notify(size_t index);
#endif
