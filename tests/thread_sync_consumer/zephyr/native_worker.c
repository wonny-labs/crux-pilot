/* Native-only runner demonstrates Sync ownership without Crux Thread. */
#include <crux/sync.h>
#include <zephyr/kernel.h>

K_THREAD_STACK_DEFINE(native_stack, 2048);
static struct k_thread native_thread;
static crux_mutex_t shared_mutex;
static int result;
static void native_worker(void *unused1, void *unused2, void *unused3)
{
    (void)unused1;
    (void)unused2;
    (void)unused3;
    result = crux_mutex_lock(shared_mutex, 1000) != CRUX_OK ||
             crux_mutex_lock(shared_mutex, 0) != CRUX_ERR_INVALID_STATE ||
             crux_mutex_unlock(shared_mutex) != CRUX_OK;
}
int component_native_sync_worker(crux_mutex_t mutex)
{
    shared_mutex = mutex;
    if (k_thread_create(&native_thread, native_stack, K_THREAD_STACK_SIZEOF(native_stack),
                        native_worker, NULL, NULL, NULL, 5, 0, K_NO_WAIT) == NULL ||
        k_thread_join(&native_thread, K_MSEC(2000)) != 0) {
        return 1;
    }
    return result;
}
