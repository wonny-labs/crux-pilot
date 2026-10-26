#define _POSIX_C_SOURCE 200809L // NOLINT(bugprone-reserved-identifier): POSIX feature selector
#include <assert.h>
#include <crux/logging.h>
#include <errno.h>
#include <pthread.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
static int saved, channel[2];
static void capture(void)
{
    assert(pipe(channel) == 0);
    saved = dup(STDERR_FILENO);
    assert(saved >= 0);
    assert(dup2(channel[1], STDERR_FILENO) == STDERR_FILENO);
    close(channel[1]);
}
static void finish(char *out, size_t size)
{
    assert(dup2(saved, STDERR_FILENO) == STDERR_FILENO);
    close(saved);
    size_t n = 0;
    ssize_t got;
    while ((got = read(channel[0], out + n, size - 1 - n)) > 0)
        n += (size_t)got;
    assert(got == 0);
    out[n] = 0;
    close(channel[0]);
}
static void output(void)
{
    char actual[1024];
    capture();
    assert(crux_log_write(CRUX_LOG_DEBUG, "app", "hidden") == CRUX_OK);
    assert(crux_log_write(CRUX_LOG_INFO, "controller", "healthy 100% %s") == CRUX_OK);
    assert(crux_log_write(CRUX_LOG_WARN, "runtime.gc", "pressure") == CRUX_OK);
    assert(crux_log_write(CRUX_LOG_ERROR, "app", "failed") == CRUX_OK);
    assert(crux_log_set_level(CRUX_LOG_DEBUG) == CRUX_OK);
    assert(crux_log_write(CRUX_LOG_DEBUG, "app", "visible") == CRUX_OK);
    finish(actual, sizeof(actual));
    assert(!strcmp(actual, "INFO controller: healthy 100% %s\nWARN runtime.gc: pressure\nERROR "
                           "app: failed\nDEBUG app: visible\n"));
}
static void *writer(void *context)
{
    unsigned id = *(unsigned *)context;
    for (unsigned i = 0; i < 8; i++) {
        char text[64];
        assert(snprintf(text, sizeof(text), "id=%u sequence=%u", id, i) > 0);
        assert(crux_log_write(CRUX_LOG_INFO, "worker", text) == CRUX_OK);
    }
    return NULL;
}
static void concurrent(void)
{
    char actual[4096];
    capture();
    pthread_t threads[4];
    unsigned ids[4] = {0, 1, 2, 3};
    for (unsigned i = 0; i < 4; i++)
        assert(pthread_create(&threads[i], NULL, writer, &ids[i]) == 0);
    for (unsigned i = 0; i < 4; i++)
        assert(pthread_join(threads[i], NULL) == 0);
    finish(actual, sizeof(actual));
    bool seen[4][8] = {{false}};
    unsigned lines = 0;
    for (char *line = strtok(actual, "\n"); line; line = strtok(NULL, "\n")) {
        unsigned id, sequence;
        int used = 0;
        assert(sscanf(line, "INFO worker: id=%u sequence=%u%n", &id, &sequence, &used) == 2);
        assert(line[used] == 0 && id < 4 && sequence < 8 && !seen[id][sequence]);
        seen[id][sequence] = true;
        lines++;
    }
    assert(lines == 32);
}
static void full(void)
{
    FILE *device = fopen("/dev/full", "w");
    assert(device);
    saved = dup(STDERR_FILENO);
    assert(saved >= 0);
    assert(dup2(fileno(device), STDERR_FILENO) == STDERR_FILENO);
    assert(crux_log_write(CRUX_LOG_ERROR, "app", "full output") == CRUX_ERR_IO);
    assert(dup2(saved, STDERR_FILENO) == STDERR_FILENO);
    close(saved);
    fclose(device);
}
static void broken(bool pending)
{
    sigset_t block, old, check;
    sigemptyset(&block);
    sigaddset(&block, SIGPIPE);
    if (pending) {
        assert(pthread_sigmask(SIG_BLOCK, &block, &old) == 0);
        assert(raise(SIGPIPE) == 0);
    }
    capture();
    close(channel[0]);
    assert(crux_log_write(CRUX_LOG_ERROR, "app", "closed pipe") == CRUX_ERR_IO);
    assert(dup2(saved, STDERR_FILENO) == STDERR_FILENO);
    close(saved);
    if (pending) {
        assert(sigpending(&check) == 0 && sigismember(&check, SIGPIPE) == 1);
        struct timespec zero = {0};
        assert(sigtimedwait(&block, NULL, &zero) == SIGPIPE);
        assert(pthread_sigmask(SIG_SETMASK, &old, NULL) == 0);
    }
}
int main(int argc, char **argv)
{
    assert(argc == 2);
    if (!strcmp(argv[1], "output"))
        output();
    else if (!strcmp(argv[1], "concurrent"))
        concurrent();
    else if (!strcmp(argv[1], "full"))
        full();
    else if (!strcmp(argv[1], "broken"))
        broken(false);
    else if (!strcmp(argv[1], "pending"))
        broken(true);
    else
        assert(false);
    puts("actual host logging passed");
    return 0;
}
