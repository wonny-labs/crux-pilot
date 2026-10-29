#include "file_port.h"
#include <pthread.h>
#include <stdlib.h>

static pthread_mutex_t file_lock = PTHREAD_MUTEX_INITIALIZER;

void crux_file_lock(void)
{
    if (pthread_mutex_lock(&file_lock) != 0)
        abort();
}

void crux_file_unlock(void)
{
    if (pthread_mutex_unlock(&file_lock) != 0)
        abort(); /* Broken internal ownership cannot safely continue. */
}
