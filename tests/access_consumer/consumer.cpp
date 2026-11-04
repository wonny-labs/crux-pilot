#include "consumer.h"

namespace
{
const char *const operator_permissions[] = {"app.pump.write", CRUX_ACCESS_PERMISSION_SELF};
const crux_access_role_t roles[] = {{"operator", operator_permissions, 2}};
const crux_access_policy_t policy = {roles, 1, 900, 28800, nullptr};
} // namespace

crux_access_result_t access_consumer_start()
{
    return crux_access_init(&policy);
}
