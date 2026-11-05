/* Shared by the native_sim application's files. */
#ifndef WEB_PROBE_H
#define WEB_PROBE_H

#include "crux_web_adapter.h"

crux_web_auth_t web_probe_authenticate(const crux_web_request_head_t *head,
                                       crux_access_session_t *session, void *context);
void web_probe_fill_large_body(void);
/* The socket shim, on the server thread. After @p skip more sends, one send returns -EAGAIN. */
void web_probe_fail_send_after(unsigned skip);
void web_probe_set_next_send_buffer(int size);
unsigned web_probe_eagain_count(void);

#endif /* WEB_PROBE_H */
