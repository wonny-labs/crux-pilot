#include "fake_attachment.h"

#define FAKE_ATTACHMENT_MAX 8u

struct crux_ssh_attachment {
    crux_access_session_t session;
};

static struct crux_ssh_attachment pool[FAKE_ATTACHMENT_MAX];
static size_t used;

crux_ssh_attachment_t *crux_ssh_fake_attachment(crux_access_session_t session)
{
    if (used == FAKE_ATTACHMENT_MAX) {
        return NULL;
    }
    pool[used].session = session;
    return &pool[used++];
}

void crux_ssh_fake_attachment_reset(void)
{
    used = 0;
}

crux_access_session_t crux_ssh_attachment_session(const crux_ssh_attachment_t *attachment)
{
    crux_access_session_t none = {0};

    return attachment != NULL ? attachment->session : none;
}
