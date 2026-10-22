#include <crux/execution.h>
#include <stdio.h>

typedef struct {
    crux_mutex_t mutex;
    crux_semaphore_t ready;
    int value;
    crux_status_t result;
} firmware_t;

static void update(void *argument) {
    firmware_t *firmware = argument;
    firmware->result = crux_mutex_lock(firmware->mutex, 1000);
    if (firmware->result != CRUX_OK)
        return;
    firmware->value = 12;
    firmware->result = crux_mutex_unlock(firmware->mutex);
    if (firmware->result == CRUX_OK)
        firmware->result = crux_semaphore_give(firmware->ready);
}

int crux_firmware_run(void) {
    static firmware_t firmware;
    crux_thread_options_t options = {.stack_size_bytes = 4096,
                                     .priority = CRUX_THREAD_PRIORITY_NORMAL};
    crux_thread_t worker;
    if (crux_mutex_create(&firmware.mutex) != CRUX_OK)
        return 1;
    if (crux_semaphore_create(0, 1, &firmware.ready) != CRUX_OK) {
        (void)crux_mutex_destroy(&firmware.mutex);
        return 2;
    }
    if (crux_thread_create(&options, update, &firmware, &worker) != CRUX_OK) {
        (void)crux_semaphore_destroy(&firmware.ready);
        (void)crux_mutex_destroy(&firmware.mutex);
        return 3;
    }
    crux_status_t ready = crux_semaphore_take(firmware.ready, 1000);
    crux_status_t joined = crux_thread_join(&worker, 2000);
    if (joined != CRUX_OK) {
        /* Test composition terminates on failure; do not reclaim live worker resources. */
        return 4;
    }
    int result = ready == CRUX_OK && firmware.result == CRUX_OK && firmware.value == 12 ? 0 : 5;
    if (crux_semaphore_destroy(&firmware.ready) != CRUX_OK)
        result = 6;
    if (crux_mutex_destroy(&firmware.mutex) != CRUX_OK)
        result = 7;
    return result;
}
