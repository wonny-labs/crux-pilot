#ifndef CRUX_QUEUE_PORT_H
#define CRUX_QUEUE_PORT_H
#include <crux/status.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <zephyr/kernel.h>
typedef struct {
    k_timeout_t timeout;
    bool poll;
} queue_deadline_t;
bool crux_queue_context_allowed(void);
crux_status_t crux_queue_make_deadline(uint32_t ms, queue_deadline_t *out);
crux_status_t crux_queue_lock(const queue_deadline_t *deadline);
void crux_queue_unlock(void);
crux_status_t crux_queue_prepare_wait(size_t index);
crux_status_t crux_queue_wait(size_t index, const queue_deadline_t *deadline);
void crux_queue_notify(size_t index);
#endif
