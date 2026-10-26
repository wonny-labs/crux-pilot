#include "fault_provider.h"
#include <stdio.h>
int controller_configuration_scenario(void);
int main(void)
{
    config_fault_reset();
    int result = controller_configuration_scenario();
    printf("CONFIGURATION_CONTROLLER_%s result=%d\n", result ? "FAIL" : "PASS", result);
    return result ? 1 : 0;
}
