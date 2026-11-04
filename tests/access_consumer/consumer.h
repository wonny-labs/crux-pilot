#ifndef CRUX_ACCESS_CONSUMER_H
#define CRUX_ACCESS_CONSUMER_H

#include <crux/access.h>

#ifdef __cplusplus
extern "C" {
#endif

crux_access_result_t access_consumer_may_write(crux_access_session_t session);

#ifdef __cplusplus
}
#endif

#endif
