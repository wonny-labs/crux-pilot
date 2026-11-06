#include <crux/reboot.h>

static_assert(CRUX_REBOOT_OK == 0, "the header must be usable from C++");
static_assert(CRUX_REBOOT_REASON_SIZE == 64u, "63 bytes of text and a terminator");

namespace
{
bool allow_everything(void *, crux_reboot_refusal_t *)
{
    return true;
}

// A C++17 consumer builds the configuration by assignment, so every member must be nameable, and
// the zero value must be the unset policy.
[[maybe_unused]] crux_reboot_config_t make_config()
{
    crux_reboot_config_t config{};
    static_assert(CRUX_REBOOT_POLICY_UNSET == 0, "a zeroed configuration states no policy");
    config.policy_kind = CRUX_REBOOT_POLICY_CALLBACK;
    config.policy = allow_everything;
    config.policy_context = nullptr;
    config.wait_sources = nullptr;
    config.wait_source_count = 0u;
    return config;
}
} // namespace
