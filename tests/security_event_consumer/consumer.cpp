#include <crux/security_event.h>

namespace
{
const crux_security_event_project_id_t project_ids[] = {
    {0x8000, "app.pump_started", CRUX_SECURITY_EVENT_CHANGE},
};
} // namespace

crux_status_t consumer_cpp_start()
{
    return crux_security_event_init(project_ids, 1);
}

uint32_t consumer_cpp_emitted()
{
    crux_security_event_stats_t stats{};
    return crux_security_event_get_stats(&stats) == CRUX_OK ? stats.emitted : 0;
}
