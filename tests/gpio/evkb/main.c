#include "consumer.h"

int main(void)
{
    bool active = false;
    return read_logical_input(&active) == CRUX_OK ? 0 : 1;
}
