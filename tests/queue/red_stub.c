/* Test-only absence baseline; never linked into production. */
#include <crux/queue.h>
crux_status_t crux_queue_create(size_t c, size_t s, crux_queue_t *o)
{
    (void)c;
    (void)s;
    (void)o;
    return CRUX_ERR_NOT_SUPPORTED;
}
crux_status_t crux_queue_send(crux_queue_t q, const void *p, size_t s, uint32_t t)
{
    (void)q;
    (void)p;
    (void)s;
    (void)t;
    return CRUX_ERR_NOT_SUPPORTED;
}
crux_status_t crux_queue_receive(crux_queue_t q, void *p, size_t s, uint32_t t)
{
    (void)q;
    (void)p;
    (void)s;
    (void)t;
    return CRUX_ERR_NOT_SUPPORTED;
}
crux_status_t crux_queue_destroy(crux_queue_t *q)
{
    (void)q;
    return CRUX_ERR_NOT_SUPPORTED;
}
