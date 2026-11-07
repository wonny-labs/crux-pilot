/* Compile only, for a board: no test controls a real device's ID, and no case that needs it runs.
 */
#include "psa_port_cases.h"

void psa_port_test_device_id(const uint8_t *id, size_t length)
{
    (void)id;
    (void)length;
}
