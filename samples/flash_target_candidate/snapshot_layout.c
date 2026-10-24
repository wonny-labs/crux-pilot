/* SPDX-License-Identifier: Apache-2.0 */
/* Compile-time witness for literal MMIO accesses in pre-C assembly. */
#include <fsl_device_registers.h>
#include <stddef.h>
#include <stdint.h>
_Static_assert(WDOG1_BASE == 0x400b8000 && WDOG2_BASE == 0x400d0000, "WDOG bases");
_Static_assert(RTWDOG_BASE == 0x400bc000 && SRC_BASE == 0x400f8000, "reset bases");
_Static_assert(IOMUXC_GPR_BASE == 0x400ac000, "FlexRAM base");
_Static_assert(offsetof(WDOG_Type, WCR) == 0 && sizeof(((WDOG_Type *)0)->WCR) == 2, "WCR halfword");
_Static_assert(offsetof(WDOG_Type, WMCR) == 8 && sizeof(((WDOG_Type *)0)->WMCR) == 2,
               "WMCR halfword");
_Static_assert(offsetof(RTWDOG_Type, CS) == 0 && sizeof(((RTWDOG_Type *)0)->CS) == 4,
               "RTWDOG word");
_Static_assert(offsetof(SRC_Type, SRSR) == 8 && sizeof(((SRC_Type *)0)->SRSR) == 4,
               "reset cause word");
_Static_assert(offsetof(IOMUXC_GPR_Type, GPR16) == 64 && offsetof(IOMUXC_GPR_Type, GPR17) == 68,
               "FlexRAM words");
_Static_assert(WDOG_WCR_WDE_MASK == 4 && WDOG_WMCR_PDE_MASK == 1 && RTWDOG_CS_EN_MASK == 0x80,
               "armed masks");
_Static_assert(SCB_BASE + offsetof(SCB_Type, CCR) == 0xe000ed14, "cache control");
_Static_assert(MPU_BASE + offsetof(MPU_Type, CTRL) == 0xe000ed94, "MPU control");
/* Read-only island assembly literal witnesses. */
_Static_assert(FLEXSPI_BASE == 0x402a8000, "selected FlexSPI controller");
_Static_assert(offsetof(FLEXSPI_Type, STS0) == 0xe0, "controller status offset");
_Static_assert(FLEXSPI_STS0_SEQIDLE_MASK == 1 && FLEXSPI_STS0_ARBIDLE_MASK == 2,
               "controller idle is not NOR ready");
_Static_assert(FLEXSPI_MCR0_SWRESET_MASK == 1 && FLEXSPI_MCR0_MDIS_MASK == 2,
               "controller inaccessible modes");
_Static_assert(SCB_BASE + offsetof(SCB_Type, VTOR) == 0xe000ed08, "VTOR literal");
_Static_assert(SCB_BASE + offsetof(SCB_Type, CPACR) == 0xe000ed88, "CPACR literal");
_Static_assert(FPU_BASE + offsetof(FPU_Type, FPCCR) == 0xe000ef34, "FPCCR literal");

_Static_assert(CCM_BASE + offsetof(CCM_Type, CSCMR1) == 0x400fc01c, "serial mux literal");
_Static_assert(CCM_CSCMR1_FLEXSPI_CLK_SEL_MASK == 0x60000000 &&
                   CCM_CSCMR1_FLEXSPI_CLK_SEL_SHIFT == 29,
               "serial mux selector");
/* The pre-clock admission table was audited only with these source clocks unchanged. */
#if defined(CONFIG_INIT_SYS_PLL) || defined(CONFIG_INIT_ENET_PLL) ||                               \
    defined(CONFIG_INIT_VIDEO_PLL) || defined(CONFIG_IMX_USDHC) ||                                 \
    defined(CONFIG_USB_DC_NXP_EHCI) || defined(CONFIG_UDC_NXP_EHCI) ||                             \
    defined(CONFIG_UHC_NXP_EHCI)
#error "candidate serial-clock source audit must be repeated for this configuration"
#endif
