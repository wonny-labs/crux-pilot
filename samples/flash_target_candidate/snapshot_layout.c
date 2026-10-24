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
