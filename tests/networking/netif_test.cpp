#include <gtest/gtest.h>
extern "C" {
#include "netif_port.h"
}
#include <cstring>

static crux_status_t observed_result;
static crux_netif_status_t observed;
extern "C" crux_status_t crux_netif_observe(crux_netif_status_t *out) {
    *out = observed;
    return observed_result;
}

TEST(Netif, RejectsNull) {
    EXPECT_EQ(crux_netif_get_status(nullptr), CRUX_ERR_INVALID_ARGUMENT);
}
TEST(Netif, PreservesOutputWhenObservationFails) {
    for (auto error : {CRUX_ERR_NOT_READY, CRUX_ERR_IO}) {
        crux_netif_status_t output{};
        output.link = CRUX_NETIF_LINK_UP;
        const auto saved = output;
        observed = {};
        observed_result = error;
        EXPECT_EQ(crux_netif_get_status(&output), error);
        EXPECT_EQ(std::memcmp(&saved, &output, sizeof output), 0);
    }
}
TEST(Netif, AddressRemainsConfiguredWhenLinkAndAdminDown) {
    observed_result = CRUX_OK;
    observed = {false, CRUX_NETIF_LINK_DOWN, true, {192, 168, 77, 2}};
    crux_netif_status_t output{};
    ASSERT_EQ(crux_netif_get_status(&output), CRUX_OK);
    EXPECT_FALSE(output.administrative_up);
    EXPECT_EQ(output.link, CRUX_NETIF_LINK_DOWN);
    EXPECT_TRUE(output.ipv4_available);
    EXPECT_EQ(output.ipv4_address[3], 2);
}
TEST(Netif, ClearsBytesOfAbsentAddressAndReportsUnknownCarrier) {
    observed_result = CRUX_OK;
    observed = {true, CRUX_NETIF_LINK_UNKNOWN, false, {1, 2, 3, 4}};
    crux_netif_status_t output{};
    ASSERT_EQ(crux_netif_get_status(&output), CRUX_OK);
    EXPECT_EQ(output.link, CRUX_NETIF_LINK_UNKNOWN);
    for (auto octet : output.ipv4_address)
        EXPECT_EQ(octet, 0);
}
TEST(Netif, FiltersUnspecifiedMulticastBroadcastAndLinkLocal) {
    const uint32_t mask = 0xffffff00u;
    for (auto address : {std::array<uint8_t, 4>{0, 0, 0, 0},
                         {224, 1, 2, 3},
                         {255, 255, 255, 255},
                         {192, 168, 77, 255},
                         {169, 254, 1, 2}})
        EXPECT_FALSE(crux_netif_ipv4_eligible(address.data(), mask));
    const uint8_t valid[] = {192, 168, 77, 2};
    const uint8_t loopback[] = {127, 0, 0, 1};
    EXPECT_TRUE(crux_netif_ipv4_eligible(valid, mask));
    EXPECT_TRUE(crux_netif_ipv4_eligible(loopback, mask));
    const uint32_t point_to_point = 0xfffffffeu;
    const uint8_t endpoint[] = {192, 168, 77, 3};
    EXPECT_TRUE(crux_netif_ipv4_eligible(endpoint, point_to_point));
}
