# Flash XIP phase evidence — Issue #22 AC3

**Complete code/data/exception/bus-master closure is unknown and remains a
physical-write blocker.** This matrix records software evidence and open proof
obligations; it does not qualify a write-enabled image, adopt the experimental
driver or authorise hardware operation. It supplements the
[storage assessment](storage-configuration-assessment.md) and
[isolated PoC](flash-safety-poc.md).

## Identities and evidence classes

Documentation baseline: Crux main `32405ad79f08492f2bbd11d98ccf0d70af5c338e`.
No target build or execution was performed for this document.

| Evidence | Reproducible identity and scope |
| --- | --- |
| **I — inherited ELF** | Storage-assessment `flash-driver` composition, built from Crux `fd9e341ed4c49654a8498198627a84092aad912c`; ELF SHA-256 `5e21cac3aa1d025434e58bbd3f2137ac968af4ed85e2bb5d8be6dc3997979669`. [Verification record](evidence/storage-configuration/verification.json) retains configuration, linker/load ranges, selected symbols/calls and source hashes. This is the native driver compile-only image, not the normal controller or the PoC. |
| **P — PoC ELF** | Implementation `10b32a732c140c20f936b3b695296d0b098ddf5e`; ELF SHA-256 `eb313d4c8df82e9c45b32be4bfadb02f16e4437742928f612621e2aec575fc0c`. [Placement](evidence/flash-safety-poc/placement.json), [selected disassembly](evidence/flash-safety-poc/selected-disassembly.txt), [original/generated hashes](evidence/flash-safety-poc/source-hashes.json) and [review provenance](evidence/flash-safety-poc/review-record.md) identify the limited experiment. Its configuration is distinct from I. |
| **S — pinned source inference** | Zephyr `684c9e8f32e4373a21098559f748f06915f950c9`; NXP HAL `2c2f28ac333e995d7279777409817c4b4f92c1ec`. Source control flow establishes possible paths, not execution or complete placement of every reachable dependency. |

Addresses below are instruction/data addresses; a Thumb function symbol may have
bit zero set. Both images use ITCM code and SDRAM data whose load images reside
in Flash. RAM VMAs alone do not establish that startup copied them before use,
or that memory remains accessible during controller/cache changes.

## Phase matrix

| Phase | Established evidence | Remaining closure obligation / disposition |
| --- | --- | --- |
| Startup / driver probe | **S:** Native init shallow-copies the controller device into RAM, then probe changes clock, LUT and NOR mode. Probe's final set-device-config error returns before `_exit`; other failures reach reset/IRQ cleanup, and some XIP failures intentionally spin. **I:** ITCM relocation load segment has VMA `0x0`, Flash LMA `0x600022c0`, size `0x2160`; RAM data also has a Flash LMA. **P:** Selected runtime placement is established, not the whole boot path. | Audit boot hooks, relocation/copy order, SDRAM initialisation and accessibility, all probe branches/constants/LUT/clock dependencies and failure cleanup. Enabling Flash adds this risk even without an application write. Startup closure **unknown**. |
| Normal read / idle | **I:** Unchanged normal reference configuration has `CONFIG_FLASH=n`; the separate inherited driver image has Flash enabled. Normal XIP code and constants intentionally reside in Flash. **S:** Native reads validate device bounds; this is not proof of safe reads during program/erase. | Define when Flash is readable and controller mapping restored, serialise all Flash clients and rule out overlapping requests. A booted controller or successful ordinary read cannot qualify a busy interval. |
| Entry / preparation | **I:** Write calls ITCM veneer `0x2158` to Flash `memcpy` at `0x6001907c`; write buffer `0x80005715` and probe LUT `0x8000553c` are SDRAM. **S:** Buffer copy precedes WREN/program and is followed by bus-idle draining; XIP locks IRQs for the whole request. | Phase ordering means this copy's Flash placement alone is not a demonstrated busy-phase fetch. Prove entry began ready, prefetch drained, input lifetime/ownership and RAM access valid; inspect every helper, literal, veneer and indirect device/API reference for the final composition. |
| Program / erase busy | **I:** Selected NOR/MEMC/HAL text is ITCM, but `memc_flexspi_config_0` is Flash `0x6001b4b0`. Transfer loads the copied device's config pointer at `0x1fe4`, dereferences it at `0x1fea` for register base `0x402a8000`; status polling reaches this path while NOR may be busy. Reset repeats the dependency at `0x1e90–0x1e92`. **P:** Writable MEMC config is SDRAM `0x800000ec`; selected transfer/HAL/NOR functions are ITCM. | **Concrete inherited data-closure failure.** Cache residency cannot remove it. PoC placement removes this demonstrated config dependency only. Full transitive code/data closure, stack, constants, port-adjustment copy branch and other indirect references remain **unknown**; host stubs do not prove them. |
| Successful completion | **S/I:** Ready polling precedes controller reset, IRQ unlock and Flash cache helper via veneer `0x2130` to `0x6000f02c`. Native code discards errors, so that sequence can run without confirmed readiness. **P:** Checked runtime command/status failures enter terminal before ordinary cleanup/return on XIP uncertainty. | Prove ready status is trustworthy, reset and cache operations terminate and restore required state, and ordinary return/next-page copy occurs only when Flash is executable/readable. Controller buffer reset does not establish NOR internal completion or general instruction-cache coherence. |
| Failure / exhausted wait | **I/S:** WREN/command/status errors can be ignored; NOR and nested MEMC/HAL waits lack software deadlines. **P:** Checked error paths and 1,024-observation budgets lead to a terminal whose ITCM body at `0x227c` contains only `nop` and a local back-branch. Host fault tests establish selected stop/return behavior. | Terminal instruction closure is established; whole-system terminal safety is **unknown**. No qualified advancing time source, wall-time bound or recovery policy exists. Inherited `sys_clock_cycle_get_32` at Flash `0x6000e00c` calls Flash `elapsed`; it cannot simply be inserted into busy code. Bus accesses can stall, NMI/fault/watchdog can intervene, and elapsed time or reset does not prove ready state. |
| Interrupts / exceptions | **I:** Retained ELF inspection places vectors at Flash `0x60002000`, NMI at `0x600127ea` and `z_arm_fault` at `0x60006b9c`; SRAM vector relocation is disabled. **S:** BASEPRI-based critical sections do not establish NMI/fault closure. **P:** No complete exception/vector audit is claimed. | Audit vector fetches, NMI/fault/zero-latency paths, their stacks/constants/callees and watchdog/reset behavior for the final image. IRQ locking and a RAM terminal do not supply this proof. **Physical-write blocker.** |
| DMA / other bus masters | **I/S:** Inherited controller includes Ethernet DMA despite general `CONFIG_DMA=n`; DTCM ENET buffer placement does not establish bus-master quiescence. **P:** Standalone experiment supplies no ownership/quiescence proof for a controller composition. | Inventory every enabled master, descriptors/buffers and Flash references; establish exclusive ownership and verified quiescence throughout reconfiguration/busy/uncertain state. Neither IRQ masking nor disabling generic DMA is sufficient. Closure **unknown**. |

## Source and reproduction anchors

Pinned [NOR driver](https://github.com/zephyrproject-rtos/zephyr/blob/684c9e8f32e4373a21098559f748f06915f950c9/drivers/flash/flash_mcux_flexspi_nor.c)
contains busy polling (312–336), write/erase (406–574), probe/cleanup (1585–1697)
and controller copy/init (1700–1758).
Pinned [MEMC driver](https://github.com/zephyrproject-rtos/zephyr/blob/684c9e8f32e4373a21098559f748f06915f950c9/drivers/memc/memc_mcux_flexspi.c)
contains MMIO getters (81–88), clock/configuration (100–267), reset/transfer
(269–311) and the configuration initializer. The
[MMIO ROM fallback](https://github.com/zephyrproject-rtos/zephyr/blob/684c9e8f32e4373a21098559f748f06915f950c9/include/zephyr/sys/device_mmio.h#L599)
explains why copying the device leaves a Flash configuration dependency.
[HAL waits](https://github.com/zephyrproject-rtos/hal_nxp/blob/2c2f28ac333e995d7279777409817c4b4f92c1ec/mcux/mcux-sdk-ng/drivers/flexspi/fsl_flexspi.c),
[relocation selection](https://github.com/zephyrproject-rtos/zephyr/blob/684c9e8f32e4373a21098559f748f06915f950c9/drivers/flash/CMakeLists.txt#L107)
and [startup preparation](https://github.com/zephyrproject-rtos/zephyr/blob/684c9e8f32e4373a21098559f748f06915f950c9/arch/arm/core/cortex_m/prep_c.c#L197)
are source anchors, not completed closure audits.

The [inherited pointer record](evidence/flash-acceptance/inherited-pointer-chain.json)
is also preserved in this PR; its original investigation filename was
`inherited-config-pointer-chain.json` (SHA-256
`a17a704f145148d87d679ebb68af9683fb70daafc6f001253ba1d921e0cc3863`),
`inherited-flash-driver-disassembly.txt` (SHA-256
`f1e9deda5729e6ba3382a09b151610af3a00e0246bfbb7830d872259a3e4d9a8`)
and `inherited-flash-driver-symbols.txt` (SHA-256
`d4354c9836dbb9f76dfa1158c227758b052bf81cebd5a37368789bc6ae51427d`).
These identify the direct pointer/exception findings separately from the committed
selected-symbol record; they are not new runtime observations.

Reproduce I with the exact composition/commands in the
[storage assessment](storage-configuration-assessment.md#non-destructive-verification),
and P with `scripts/test-flash-poc.sh` as documented in the
[PoC verification](flash-safety-poc.md#verification), using the recorded revisions
and Linux SDK 1.0.1/GNU Arm 14.3.0 environment. Keep the resulting ELF, map,
configuration and linker together; verify the ELF hash before comparing addresses.
Use SDK `arm-zephyr-eabi-readelf -lW`, `arm-zephyr-eabi-nm -n` and
`arm-zephyr-eabi-objdump -dr` on that ELF. Follow pointer loads through the ELF's
file-backed segments; a symbol table or direct-call list alone misses data and
indirect edges. Source inference must be reconciled with generated configuration
and final reachable branches, including startup and cleanup.

AC3 therefore has a phase-specific evidence inventory, **not complete closure**.
Separate independent review must assess this author-maintained matrix. Physical
work still requires the approved final remediation/policy, complete closure and
the board/part/data/erase/recovery handoff gates in the linked documents.
