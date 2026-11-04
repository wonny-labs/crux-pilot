#include <crux/security_event.h>

int main(void)
{
    uint32_t cursor = 0;
    crux_security_event_record_t record;
    const crux_access_session_t session = {1};
    return crux_security_event_read(session, &cursor, &record) == CRUX_SECURITY_READ_OK ? 0 : 1;
}
