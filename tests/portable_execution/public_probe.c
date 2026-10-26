#include <crux/sync.h>

#if defined(__arm__) && (!defined(__thumb__) || __ARM_ARCH != 7)
#error "The supported EVKB consumer must use the Cortex-M7 Thumb instruction set"
#endif

#if defined(__ZEPHYR__) || defined(KERNEL)
#error "Native environment requirements leaked into the portable consumer"
#endif
#if __has_include("thread_internal.h") ||                                                          \
                  __has_include("thread_native_types.h") ||                                        \
                                __has_include("sync_internal.h") ||                                \
                                              __has_include("sync_native_types.h")
#error "Private Thread/Sync headers leaked into the portable consumer"
#endif
#if __has_include(<zephyr/kernel.h>)
#error "Zephyr headers are reachable through the portable usage target"
#endif

/* This translation unit deliberately consumes only the published target. */
int execution_public_probe(crux_mutex_t mutex)
{
    return crux_mutex_lock(mutex, 0) == CRUX_OK ? crux_mutex_unlock(mutex) : 1;
}
