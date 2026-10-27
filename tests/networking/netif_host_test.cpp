#include <crux/netif.h>
#include <cstring>
#include <gtest/gtest.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <netinet/in.h>

static ifaddrs entry;
static sockaddr_in address, mask;
static int fail_observation;
static unsigned int frees;
extern "C" int crux_test_getifaddrs(ifaddrs **out)
{
    *out = &entry;
    return fail_observation;
}
extern "C" void crux_test_freeifaddrs(ifaddrs *)
{
    ++frees;
}

TEST(NetifHost, SelectedInterfaceAndAddressAreIndependentOfAdmin)
{
    address = {};
    address.sin_family = AF_INET;
    const uint8_t bytes[] = {192, 168, 77, 2};
    std::memcpy(&address.sin_addr, bytes, 4);
    mask = {};
    const uint8_t netmask[] = {255, 255, 255, 0};
    std::memcpy(&mask.sin_addr, netmask, 4);
    entry = {};
    entry.ifa_name = const_cast<char *>("test0");
    entry.ifa_addr = reinterpret_cast<sockaddr *>(&address);
    entry.ifa_netmask = reinterpret_cast<sockaddr *>(&mask);
    fail_observation = 0;
    frees = 0;
    crux_netif_status_t status{};
    ASSERT_EQ(crux_netif_get_status(&status), CRUX_OK);
    EXPECT_FALSE(status.administrative_up);
    EXPECT_EQ(status.link, CRUX_NETIF_LINK_UNKNOWN);
    EXPECT_TRUE(status.ipv4_available);
    EXPECT_EQ(status.ipv4_address[3], 2);
    EXPECT_EQ(frees, 1u);
    entry.ifa_flags = IFF_UP | IFF_RUNNING;
    ASSERT_EQ(crux_netif_get_status(&status), CRUX_OK);
    EXPECT_EQ(status.link, CRUX_NETIF_LINK_UNKNOWN);
    entry.ifa_name = const_cast<char *>("other");
    EXPECT_EQ(crux_netif_get_status(&status), CRUX_ERR_NOT_READY);
    EXPECT_TRUE(status.ipv4_available);
    fail_observation = -1;
    EXPECT_EQ(crux_netif_get_status(&status), CRUX_ERR_IO);
    EXPECT_TRUE(status.ipv4_available);
    EXPECT_EQ(frees, 3u);
}
