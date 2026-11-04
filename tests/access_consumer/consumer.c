#include "consumer.h"

crux_access_result_t access_consumer_may_write(crux_access_session_t session)
{
    return crux_access_check(session, "app.pump.write");
}
