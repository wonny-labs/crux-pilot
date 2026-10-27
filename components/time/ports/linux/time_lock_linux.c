#include "interrupt.h"
#include "realtime_port.h"
#include <pthread.h>

static pthread_mutex_t realtime_lock = PTHREAD_MUTEX_INITIALIZER;

crux_status_t crux_realtime_impl_lock(void)
{
    if (crux_is_in_isr()) {
        return CRUX_ERR_NOT_SUPPORTED;
    }
    return pthread_mutex_lock(&realtime_lock) == 0 ? CRUX_OK : CRUX_ERR_IO;
}

void crux_realtime_impl_unlock(void)
{
    (void)pthread_mutex_unlock(&realtime_lock);
}
