#include <crux/execution.h>
#include <cstdio>
extern "C" int crux_firmware_run(void);
int main() {
    crux_mutex_t mutex{};
    if (crux_mutex_create(&mutex) != CRUX_OK || crux_mutex_lock(mutex, 0) != CRUX_OK ||
        crux_mutex_unlock(mutex) != CRUX_OK || crux_mutex_destroy(&mutex) != CRUX_OK)
        return 1;
    int result = crux_firmware_run();
    if (result == 0)
        std::puts("FIRMWARE_CONSUMER passed C17/C++17");
    return result;
}
