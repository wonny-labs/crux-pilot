#define _XOPEN_SOURCE 600
#include "controller.h"
#include "runtime.h"
#include "serial_host.h"
#include <assert.h>
#include <fcntl.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <unistd.h>
static void peer(int master, size_t size) {
    unsigned char frame[16];
    size_t offset = 0;
    while (offset < size) {
        struct pollfd p = {.fd = master, .events = POLLIN};
        assert(poll(&p, 1, 1000) > 0);
        ssize_t count = read(master, frame + offset, size - offset);
        if (count < 0)
            continue; /* Slave may still be opening. */
        assert(count > 0);
        offset += (size_t)count;
    }
    assert(frame[0] == 0xc3 && frame[1] == 0 && frame[2] == 0xff && frame[3] == 0x51);
    frame[3] = 0x41;
#ifdef STALE_ACK
    const unsigned char old_identity[] = {0x19, 0x20, 0x39, 0x5a};
    for (size_t i = 0; i < sizeof(old_identity); ++i)
        frame[4 + i] = old_identity[i];
#endif
    for (offset = 0; offset < size; ++offset) {
        assert(write(master, frame + offset, 1) == 1);
        usleep(1000); /* Deliberately split the response into short reads. */
    }
    usleep(30000); /* Keep the peer alive through acknowledgement and cleanup. */
}
int main(void) {
    int master = posix_openpt(O_RDWR | O_NOCTTY);
    assert(master >= 0 && grantpt(master) == 0 && unlockpt(master) == 0);
    assert(crux_serial_host_bind(0, ptsname(master)) == CRUX_OK);
    pid_t child = fork();
    assert(child >= 0);
    if (child == 0) {
#ifdef CONTROLLER
        peer(master, 8);
#else
        peer(master, 16);
#endif
        close(master);
        _exit(0);
    }
#ifdef CONTROLLER
#ifdef STALE_ACK
    assert(serial_controller_run(0, UINT32_C(0x12345678)) == CRUX_ERR_IO);
#else
    assert(serial_controller_run(0, UINT32_C(0x12345678)) == CRUX_OK);
#endif
#else
    const crux_serial_config_t config = {115200, 8, 1, CRUX_SERIAL_PARITY_NONE};
    serial_runtime_result_t result;
#ifdef CPP_CONSUMER
    assert(serial_cpp_transaction(0, &config, UINT64_C(0x3919ff005a), 200, &result) == CRUX_OK);
#else
    assert(serial_runtime_transaction(0, &config, UINT64_C(0x3919ff005a), 200, &result) == CRUX_OK);
#endif
    assert(result.transaction_status == CRUX_OK && result.cleanup_status == CRUX_OK);
#endif
    int status;
    assert(waitpid(child, &status, 0) == child && WIFEXITED(status) && WEXITSTATUS(status) == 0);
    close(master);
    puts("binary transaction acknowledged, partial response handled, cleanup complete");
    return 0;
}
