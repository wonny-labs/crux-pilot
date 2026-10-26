#include "logging_port.h"
#include <errno.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <time.h>
bool logging_port_context_allowed(void)
{
    return true;
}
crux_status_t logging_port_write(crux_log_level_t level, const char *tag, const char *text)
{
    static const char *const names[] = {"OFF", "ERROR", "WARN", "INFO", "DEBUG"};
    /* stderr is often a pipe in a host composition. Keep its broken-reader
     * failure local to this call, without changing a process-wide handler or
     * consuming a SIGPIPE that was already pending for the calling thread. */
    sigset_t block, old, pending;
    sigemptyset(&block);
    sigaddset(&block, SIGPIPE);
    if (pthread_sigmask(SIG_BLOCK, &block, &old) != 0)
        return CRUX_ERR_IO;
    if (sigpending(&pending) != 0) {
        (void)pthread_sigmask(SIG_SETMASK, &old, NULL);
        return CRUX_ERR_IO;
    }
    bool was_pending = sigismember(&pending, SIGPIPE) == 1;
    flockfile(stderr);
    int written = fprintf(stderr, "%s %s: %s\n", names[level], tag, text);
    bool broken = written < 0 && errno == EPIPE;
    int flushed = fflush(stderr);
    broken = broken || (flushed == EOF && errno == EPIPE);
    funlockfile(stderr);
    if (broken && !was_pending) {
        const struct timespec zero = {0};
        while (sigtimedwait(&block, NULL, &zero) < 0 && errno == EINTR) {
        }
    }
    int restored = pthread_sigmask(SIG_SETMASK, &old, NULL);
    return written < 0 || flushed == EOF || restored != 0 ? CRUX_ERR_IO : CRUX_OK;
}
