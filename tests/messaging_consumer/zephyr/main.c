#include <stdio.h>
int messaging_runtime_run(void);
int main(void) {
    int result = messaging_runtime_run();
    if (result == 0)
        puts("MESSAGING_NATIVE_CONSUMER passed");
    return result;
}
