#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/kernel.h>
#include <zephyr/net/net_if.h>
#include <zephyr/net/phy.h>

#include <fsl_enet.h>

/* Private RT1062 observations; never write a MAC or PHY register here. */
BUILD_ASSERT(DT_REG_ADDR(DT_GPARENT(DT_NODELABEL(phy))) == 0x402d8000);

void network_phy_diagnostic_mac(unsigned int phase)
{
    const volatile ENET_Type *mac =
        (const volatile ENET_Type *)DT_REG_ADDR(DT_GPARENT(DT_NODELABEL(phy)));
    uint32_t eir = mac->EIR;
    uint32_t eimr = mac->EIMR;
    uint32_t ecr = mac->ECR;
    uint32_t rcr = mac->RCR;
    uint32_t tcr = mac->TCR;
    uint32_t rdar = mac->RDAR;
    uint32_t tdar = mac->TDAR;
    uint32_t mscr = mac->MSCR;
    uint32_t mmfr = mac->MMFR;
    printk("M3_NET_MAC phase=%u monotonic_ms=%lld eir=%u eimr=%u ecr=%u rcr=%u tcr=%u "
           "rdar=%u tdar=%u mscr=%u mmfr=%u\n",
           phase, (long long)k_uptime_get(), eir, eimr, ecr, rcr, tcr, rdar, tdar, mscr, mmfr);
}

void network_phy_diagnostic_after_round(void)
{
    network_phy_diagnostic_mac(2);
    struct net_if *iface = net_if_get_default();
    const struct device *phy = DEVICE_DT_GET(DT_NODELABEL(phy));
    struct phy_link_state state = {0};
    int cached = phy_get_link_state(phy, &state);
    printk("M3_NET_PHY_CACHED monotonic_ms=%lld result=%d link=%d speed=%u admin=%d running=%d "
           "carrier=%d oper=%u\n",
           (long long)k_uptime_get(), cached, state.is_up, (unsigned int)state.speed,
           net_if_is_admin_up(iface), net_if_is_up(iface), net_if_is_carrier_ok(iface),
           (unsigned int)net_if_oper_state(iface));

    /* Only after traffic: BMSR reads clear history and can interleave with the native monitor.
     * A zero return from this pinned MDIO driver does not establish transaction completion. */
    const uint16_t registers[] = {0, 1, 1, 2, 3, 4, 5, 0x1f};
    uint32_t values[ARRAY_SIZE(registers)] = {0};
    int status = cached;
    for (size_t i = 0; i < ARRAY_SIZE(registers); ++i) {
        int result = phy_read(phy, registers[i], &values[i]);
        if (result != 0) {
            status = result;
        }
        printk("M3_NET_PHY_READ index=%u reg=%u result=%d value=%u monotonic_ms=%lld\n",
               (unsigned int)i, registers[i], result, values[i], (long long)k_uptime_get());
    }
    /* ID1 agrees with the pinned NXP KSZ8081 driver; ID2 is only a plausibility check. */
    bool plausible = values[3] == 0x22 && values[4] != 0 && values[4] != 0xffff;
    printk("M3_NET_PHY_DONE reads=%u status=%d plausible=%d monotonic_ms=%lld\n",
           (unsigned int)ARRAY_SIZE(registers), status, plausible, (long long)k_uptime_get());
}
