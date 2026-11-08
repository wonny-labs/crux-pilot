/* The start of the service, with its result thrown away. A composition must not be able to ignore
 * an unset policy, so this must not compile under -Werror=unused-result. Reboot.nodiscard builds it
 * and expects the build to fail. It sits in its own directory, which clang-tidy is not asked to
 * read, because its warning is the point. */
#include <crux/reboot.h>

void reboot_nodiscard_probe(const crux_reboot_config_t *config);

void reboot_nodiscard_probe(const crux_reboot_config_t *config)
{
    crux_reboot_start(config);
}
