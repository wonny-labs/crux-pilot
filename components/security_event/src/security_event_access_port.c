/* The event port of crux_access, as a thin translation onto the public emit. It holds no state and
 * takes no lock. Access reports a name only if it knows the account, so a user is never unverified
 * client input here. */
#include "access_port.h"

#include <crux/security_event.h>

#include <string.h>

static crux_security_event_result_t result_from_access(crux_access_result_t result)
{
    switch (result) {
    case CRUX_ACCESS_OK:
        return CRUX_SECURITY_EVENT_OK;
    case CRUX_ACCESS_ERR_DENIED:
    case CRUX_ACCESS_ERR_SESSION_INVALID:
    case CRUX_ACCESS_ERR_LAST_ADMINISTRATOR:
        return CRUX_SECURITY_EVENT_DENIED;
    case CRUX_ACCESS_ERR_RETRY_LATER:
        return CRUX_SECURITY_EVENT_DELAYED;
    case CRUX_ACCESS_ERR_BUSY:
        return CRUX_SECURITY_EVENT_BUSY;
    case CRUX_ACCESS_ERR_NO_RESOURCES:
        return CRUX_SECURITY_EVENT_LIMIT;
    case CRUX_ACCESS_ERR_INVALID_ARGUMENT:
    case CRUX_ACCESS_ERR_NOT_FOUND:
    case CRUX_ACCESS_ERR_ALREADY_EXISTS:
    case CRUX_ACCESS_ERR_PASSWORD_REJECTED:
        return CRUX_SECURITY_EVENT_INVALID;
    case CRUX_ACCESS_ERR_NOT_READY:
    case CRUX_ACCESS_ERR_STORE:
        break;
    }
    return CRUX_SECURITY_EVENT_FAILED;
}

static const char *field(const char *text)
{
    return text[0] != '\0' ? text : NULL;
}

/* No default case: the compiler refuses a port event that this table does not map. */
static void fill_identifier_and_names(const crux_access_event_t *from, crux_security_event_t *to)
{
    const char *actor = field(from->actor);
    const char *account = field(from->account);
    switch (from->type) {
    case CRUX_ACCESS_EVENT_LOGIN_SUCCESS:
        to->id = CRUX_SECURITY_EVENT_ID_ACCESS_LOGIN_OK;
        to->user = account;
        break;
    case CRUX_ACCESS_EVENT_LOGIN_FAILURE:
        to->id = CRUX_SECURITY_EVENT_ID_ACCESS_LOGIN_FAILED;
        to->user = account;
        break;
    case CRUX_ACCESS_EVENT_KEY_FAILURE:
        to->id = CRUX_SECURITY_EVENT_ID_ACCESS_KEY_FAILED;
        to->user = account;
        break;
    case CRUX_ACCESS_EVENT_DELAY_APPLIED:
        to->id = CRUX_SECURITY_EVENT_ID_ACCESS_DELAYED;
        break;
    case CRUX_ACCESS_EVENT_LOGIN_BUSY:
        to->id = CRUX_SECURITY_EVENT_ID_ACCESS_LOGIN_BUSY;
        break;
    case CRUX_ACCESS_EVENT_LOGIN_NO_SESSION:
        to->id = CRUX_SECURITY_EVENT_ID_ACCESS_LOGIN_NO_SESSION;
        break;
    case CRUX_ACCESS_EVENT_LOGIN_REFUSED:
        to->id = CRUX_SECURITY_EVENT_ID_ACCESS_LOGIN_REFUSED;
        to->user = account;
        break;
    case CRUX_ACCESS_EVENT_NOT_READY:
        to->id = CRUX_SECURITY_EVENT_ID_ACCESS_NOT_READY;
        break;
    case CRUX_ACCESS_EVENT_PERMISSION_DENIED:
        to->id = CRUX_SECURITY_EVENT_ID_ACCESS_PERMISSION_DENIED;
        to->user = actor;
        to->subject = field(from->permission);
        break;
    case CRUX_ACCESS_EVENT_SESSION_INVALID:
        to->id = CRUX_SECURITY_EVENT_ID_ACCESS_SESSION_INVALID;
        break;
    case CRUX_ACCESS_EVENT_LOGOUT:
        to->id = CRUX_SECURITY_EVENT_ID_ACCESS_LOGOUT;
        to->user = actor;
        break;
    case CRUX_ACCESS_EVENT_SESSION_EXPIRED:
        to->id = CRUX_SECURITY_EVENT_ID_ACCESS_SESSION_EXPIRED;
        to->user = actor;
        break;
    case CRUX_ACCESS_EVENT_PROVISIONED:
        to->id = CRUX_SECURITY_EVENT_ID_ACCESS_PROVISIONED;
        to->user = account;
        break;
    case CRUX_ACCESS_EVENT_PROVISION_REFUSED:
        to->id = CRUX_SECURITY_EVENT_ID_ACCESS_PROVISION_REFUSED;
        break;
    case CRUX_ACCESS_EVENT_ACCOUNT_CREATED:
        to->id = CRUX_SECURITY_EVENT_ID_ACCESS_ACCOUNT_CREATED;
        to->user = actor;
        to->subject = account;
        break;
    case CRUX_ACCESS_EVENT_ACCOUNT_REMOVED:
        to->id = CRUX_SECURITY_EVENT_ID_ACCESS_ACCOUNT_REMOVED;
        to->user = actor;
        to->subject = account;
        break;
    case CRUX_ACCESS_EVENT_ACCOUNT_DISABLED:
        to->id = CRUX_SECURITY_EVENT_ID_ACCESS_ACCOUNT_DISABLED;
        to->user = actor;
        to->subject = account;
        break;
    case CRUX_ACCESS_EVENT_ACCOUNT_ENABLED:
        to->id = CRUX_SECURITY_EVENT_ID_ACCESS_ACCOUNT_ENABLED;
        to->user = actor;
        to->subject = account;
        break;
    case CRUX_ACCESS_EVENT_ACCOUNT_ROLE_CHANGED:
        to->id = CRUX_SECURITY_EVENT_ID_ACCESS_ROLE_CHANGED;
        to->user = actor;
        to->subject = account;
        break;
    case CRUX_ACCESS_EVENT_ACCOUNT_PASSWORD_RESET:
        to->id = CRUX_SECURITY_EVENT_ID_ACCESS_PASSWORD_RESET;
        to->user = actor;
        to->subject = account;
        break;
    case CRUX_ACCESS_EVENT_ACCOUNT_PASSWORD_CHANGED:
        to->id = CRUX_SECURITY_EVENT_ID_ACCESS_PASSWORD_CHANGED;
        to->user = actor;
        break;
    case CRUX_ACCESS_EVENT_ACCOUNT_KEY_ADDED:
        to->id = CRUX_SECURITY_EVENT_ID_ACCESS_KEY_ADDED;
        to->user = actor;
        to->subject = account;
        break;
    case CRUX_ACCESS_EVENT_ACCOUNT_KEY_REMOVED:
        to->id = CRUX_SECURITY_EVENT_ID_ACCESS_KEY_REMOVED;
        to->user = actor;
        to->subject = account;
        break;
    case CRUX_ACCESS_EVENT_CHANGE_FAILED:
        to->id = CRUX_SECURITY_EVENT_ID_ACCESS_CHANGE_FAILED;
        to->user = actor;
        to->subject = account;
        break;
    case CRUX_ACCESS_EVENT_CHANGE_REJECTED:
        to->id = CRUX_SECURITY_EVENT_ID_ACCESS_CHANGE_REJECTED;
        to->user = actor;
        to->subject = account;
        break;
    }
}

void crux_access_port_event(const crux_access_event_t *event)
{
    /* Zero is no identifier, so a type outside the table records as event.unknown. */
    crux_security_event_t translated = {
        .result = result_from_access(event->result),
        .channel = (uint8_t)event->origin.channel,
        .source_length = event->origin.source_length,
    };
    memcpy(translated.source, event->origin.source, sizeof(translated.source));
    fill_identifier_and_names(event, &translated);
    crux_security_event_emit(&translated);
}
