#include "psa_port_cases.h"

int main(void)
{
    return psa_port_cases_run() == 0u ? 0 : 1;
}
