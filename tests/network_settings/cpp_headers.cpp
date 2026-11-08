#include <crux/network_settings.h>

static_assert(CRUX_NETWORK_OK == 0, "the header must be usable from C++");
static_assert(CRUX_NETWORK_SETTINGS_ENTRY_COUNT == 5u, "the table's five entries");

namespace
{
// A C++17 consumer builds the settings by assignment, so every member must be nameable.
[[maybe_unused]] crux_network_settings_t make_settings()
{
    crux_network_settings_t settings{};
    settings.address = 0xC000020Au;
    settings.prefix_length = 24u;
    settings.has_gateway = false;
    settings.gateway = 0u;
    return settings;
}
} // namespace
