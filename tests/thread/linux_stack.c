#define _POSIX_C_SOURCE 200809L
#include <crux/thread.h>
#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <unistd.h>

static int failures;
#define CHECK(x)                                                                                   \
    do {                                                                                           \
        if (!(x)) {                                                                                \
            fprintf(stderr, "line %d: %s\n", __LINE__, #x);                                        \
            ++failures;                                                                            \
        }                                                                                          \
    } while (0)

/* A glibc whose minimum stack is larger than the 64 KiB the provider asks for, as on aarch64. */
#define REPORTED_MINIMUM 131072L

int __real_pthread_attr_setstacksize(pthread_attr_t *attributes, size_t size);
long __real_sysconf(int name);

static size_t requested_stack;

long __wrap_sysconf(int name)
{
    return name == _SC_THREAD_STACK_MIN ? REPORTED_MINIMUM : __real_sysconf(name);
}

int __wrap_pthread_attr_setstacksize(pthread_attr_t *attributes, size_t size)
{
    requested_stack = size;
    if (size < (size_t)REPORTED_MINIMUM) {
        return EINVAL;
    }
    return __real_pthread_attr_setstacksize(attributes, size);
}

static int ran;

static void worker(void *argument)
{
    *(int *)argument = 1;
}

int main(void)
{
    crux_thread_t thread = {0};
    crux_thread_options_t options = {.stack_size_bytes = 0,
                                     .priority = CRUX_THREAD_PRIORITY_NORMAL};
    CHECK(crux_thread_create(&options, worker, &ran, &thread) == CRUX_OK);
    CHECK(crux_thread_join(&thread, 1000) == CRUX_OK);
    CHECK(ran == 1);
    CHECK(requested_stack >= (size_t)REPORTED_MINIMUM);

    ran = 0;
    options.stack_size_bytes = 65536;
    CHECK(crux_thread_create(&options, worker, &ran, &thread) == CRUX_OK);
    CHECK(crux_thread_join(&thread, 1000) == CRUX_OK);
    CHECK(ran == 1);
    CHECK(requested_stack >= (size_t)REPORTED_MINIMUM);

    options.stack_size_bytes = 65537;
    CHECK(crux_thread_create(&options, worker, &ran, &thread) == CRUX_ERR_OUT_OF_RANGE);

    printf("Linux thread stack minimum: %d failures\n", failures);
    return failures != 0;
}
