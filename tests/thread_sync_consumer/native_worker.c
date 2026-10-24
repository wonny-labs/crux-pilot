/* Native test composition: the Sync consumer has no Crux Thread implementation. */
#include <crux/sync.h>
#include <pthread.h>

static crux_mutex_t shared_mutex;
static int result;
static void *native_worker(void *unused) {
    (void)unused;
    result = crux_mutex_lock(shared_mutex, 1000) != CRUX_OK ||
             crux_mutex_lock(shared_mutex, 0) != CRUX_ERR_INVALID_STATE ||
             crux_mutex_unlock(shared_mutex) != CRUX_OK;
    return NULL;
}
int component_native_sync_worker(crux_mutex_t mutex) {
    shared_mutex = mutex;
    pthread_t worker;
    if (pthread_create(&worker, NULL, native_worker, NULL) != 0 ||
        pthread_join(worker, NULL) != 0) {
        return 1;
    }
    return result;
}
