/* What the read path asks of access control, for the suites that do not link crux_access. The
 * suites with the real access control use it instead of this file. */
#include "access_check_stub.h"

#include <pthread.h>
#include <string.h>

static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
static crux_access_result_t answer = CRUX_ACCESS_OK;
static unsigned checks;
static bool permission_was_read = true;

void access_check_stub_set(crux_access_result_t result)
{
    pthread_mutex_lock(&lock);
    answer = result;
    pthread_mutex_unlock(&lock);
}

unsigned access_check_stub_count(void)
{
    pthread_mutex_lock(&lock);
    unsigned count = checks;
    pthread_mutex_unlock(&lock);
    return count;
}

bool access_check_stub_asked_for_read(void)
{
    pthread_mutex_lock(&lock);
    bool asked = permission_was_read;
    pthread_mutex_unlock(&lock);
    return asked;
}

crux_access_result_t crux_access_check(crux_access_session_t session, const char *permission)
{
    (void)session;
    pthread_mutex_lock(&lock);
    crux_access_result_t result = answer;
    checks += 1;
    permission_was_read = permission_was_read && strcmp(permission, "events.read") == 0;
    pthread_mutex_unlock(&lock);
    return result;
}
