/* Board cross-build only: this application is compiled and linked, never flashed or run. */
#include "tmp117.h"

int main(void)
{
    return tmp117_probe(TMP117_ADDRESS_GND) == CRUX_OK ? 0 : 1;
}
