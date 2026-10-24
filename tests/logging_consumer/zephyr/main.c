#include "controller.h"
#include "runtime.h"
int main(void) {
    if (logging_runtime_record(1, 0) != CRUX_OK)
        return 1;
    if (logging_cpp_record(2, -7) != CRUX_OK)
        return 2;
    if (logging_controller_state(true) != CRUX_OK)
        return 3;
    /* These statuses acknowledge attempts, not completed UART transmission.
     * Native logging continues on its own worker; there is no public drain. */
    return 0;
}
