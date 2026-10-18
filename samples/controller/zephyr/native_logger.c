#include "native_logger.h"

#include <string.h>
#include <zephyr/kernel.h>

struct logger_lookup {
    struct k_thread *thread;
    unsigned int count;
};

static void find_logger(const struct k_thread *thread, void *context) {
    struct logger_lookup *lookup = context;
    const char *name = k_thread_name_get((k_tid_t)thread);
    if (name != NULL && strcmp(name, "logging") == 0) {
        lookup->thread = (struct k_thread *)thread;
        lookup->count++;
    }
}

struct m3_logger_resources m3_logger_resources_get(void) {
    struct logger_lookup lookup = {0};
    /* Identify during traversal; stack scanning and output remain outside its lock. */
    k_thread_foreach(find_logger, &lookup);
    struct m3_logger_resources result = {.found = lookup.count, .status = -1, .priority = -1};
    if (lookup.count == 1) {
        result.status = k_thread_stack_space_get(lookup.thread, &result.unused);
        result.usable = lookup.thread->stack_info.size;
        result.priority = k_thread_priority_get(lookup.thread);
    }
    return result;
}
