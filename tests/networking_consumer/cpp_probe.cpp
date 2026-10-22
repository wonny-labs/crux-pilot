#include <crux/netif.h>
#include <unistd.h>
extern "C" int networking_cpp_close_probe(int descriptor) { return close(descriptor); }
extern "C" crux_status_t networking_cpp_netif_probe(void) {
    crux_netif_status_t status{};
    return crux_netif_get_status(&status);
}
