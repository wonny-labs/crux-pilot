/* The management header must compile for a C++ composition, which is what a product may be. */
#include "crux_web_management.h"

int main()
{
    crux_web_management_stats_t stats = {};

    crux_web_management_stats_get(&stats);
    return static_cast<int>(stats.saves_queued) + static_cast<int>(crux_web_management_init());
}
