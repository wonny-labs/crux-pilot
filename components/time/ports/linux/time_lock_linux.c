#include "time_port.h"
#include <pthread.h>

static pthread_mutex_t realtime_lock = PTHREAD_MUTEX_INITIALIZER;

bool crux_time_port_is_thread(void)
{
    return true;
}

crux_status_t crux_time_port_lock(void)
{
    return pthread_mutex_lock(&realtime_lock) == 0 ? CRUX_OK : CRUX_ERR_IO;
}

void crux_time_port_unlock(void)
{
    (void)pthread_mutex_unlock(&realtime_lock);
}
