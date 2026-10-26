/* Native application entry; portable C/C++ code is compiled separately. */
#include <stdio.h>

int component_cpp_consumer_run(void);
int main(void)
{
    int result = component_cpp_consumer_run();
    if (result == 0) {
        puts("COMPONENT_CONSUMER_PASS");
    }
    return result;
}
