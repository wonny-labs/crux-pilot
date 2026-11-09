/* SSH start for the RT1060-EVKB bench image: see ../CMakeLists.txt. */
#ifndef CRUX_MGMT_RT1060_SSH_TARGET_H
#define CRUX_MGMT_RT1060_SSH_TARGET_H

#include <stdbool.h>

/* The host key from the credential store into a static buffer that is never wiped: TEST FORM (D5),
 * allowed only under the bench declaration. Returns false when the host key, the CLI provider or
 * the listener did not start. */
bool crux_mgmt_rt1060_ssh_start(void);

#endif
