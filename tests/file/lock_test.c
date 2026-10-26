#include "file_port.h"
#include <assert.h>
#include <errno.h>
#include <pthread.h>
#include <signal.h>
#include <stdbool.h>
#include <stdlib.h>
#include <sys/resource.h>
#include <sys/wait.h>
#include <unistd.h>
static bool fail_lock;
static unsigned value;
int __real_pthread_mutex_lock(pthread_mutex_t *lock);
int __wrap_pthread_mutex_lock(pthread_mutex_t *lock)
{
    return fail_lock ? EINVAL : __real_pthread_mutex_lock(lock);
}
static void *increment(void *unused)
{
    (void)unused;
    for (unsigned i = 0; i < 10000; i++) {
        crux_file_port_lock();
        value++;
        crux_file_port_unlock();
    }
    return NULL;
}
int main(void)
{
    pthread_t a, b;
    assert(pthread_create(&a, NULL, increment, NULL) == 0);
    assert(pthread_create(&b, NULL, increment, NULL) == 0);
    assert(pthread_join(a, NULL) == 0);
    assert(pthread_join(b, NULL) == 0);
    assert(value == 20000);
    pid_t child = fork();
    assert(child >= 0);
    if (!child) {
        struct rlimit limit = {0, 0};
        assert(setrlimit(RLIMIT_CORE, &limit) == 0);
        fail_lock = true;
        crux_file_port_lock();
        _exit(0);
    }
    int status;
    assert(waitpid(child, &status, 0) == child);
    assert(WIFSIGNALED(status) && WTERMSIG(status) == SIGABRT);
    return 0;
}
