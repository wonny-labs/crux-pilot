# Maintenance storage profile — software candidate

Status: software implementation baseline conditionally approved in the user review of 24 October 2026. This is a **limited M4 validation configuration**, not a permanent Crux storage architecture. M7 concurrent controller/network/storage qualification remains pending. No production patch/layout adoption, physical execution, programming, reset, erase/write, merge or Issue closure is authorised. Do not program or boot the PR #59 candidate.

## Observable lifecycle

One exclusive native-composition owner controls startup inspection, explicit provisioning, read-only readiness scan, maintenance, then a one-way transition to normal controller operation. A failed/uncertain phase cannot implicitly reopen maintenance. All native Flash clients must be excluded except the selected composition; this experiment is not a security boundary against arbitrary native callers.

During maintenance, existing portable File and Configuration semantics apply. Before exit, close all File handles and complete acknowledged syncs; failed close/sync does not imply rollback. Cleanup close can itself flush LittleFS data, so after uncertainty the private composition must also fence lower File-area write/erase/flatten requests while allowing native handle consumption. Cleanup may then return an error; it is not a retry or evidence of persistence. Refuse the transition while handles remain. After exit:

| API family | Behaviour for valid requests |
| --- | --- |
| File open/read/write/seek/sync/close/remove | Provider/media operations return existing File NOT_READY; no live File handles may cross exit. Invalid/stale-handle and argument errors retain existing portable precedence; do not promise NOT_READY for invalid requests. |
| Configuration load/save and native provider init | Existing Configuration NOT_READY; no native read or write dispatch. |
| Configuration init/update/read of application RAM values | Existing validation/default/lifetime rules remain available; update is not persistence. |

Read-only runtime File access is deliberately not qualified by this profile. No public contract or general provider default changes. Private simulator composition first proves the gate; a target binding must independently preserve it.

## Execution design and reset boundary

Choose one RAM island rather than implement a second full-RAM application. MPU XIP denial is optional secondary defence; no particular timer is yet selected. A target integration must establish these boundaries in order:

1. Boot ROM and any predecessor image: separately inspect initial reset reason, NOR mapping/mode, TCM setup and watchdog/external supervisor state. A debugger session cannot establish cold-boot state. No such evidence exists yet.
2. Earliest application reset entry, **before SystemInit changes watchdog counters**: capture WDOG1/WDOG2 WCR and WMCR, RTWDOG CS, SRC reset cause and relevant mapping/clock state into reserved internal RAM without relying on initialized C globals, an unavailable stack, or unestablished TCM layout. Unknown or armed state refuses persistence. The snapshot path also rejects inherited enabled MPU or D-cache before relying on OCRAM2 stores; this is a narrow supported-entry candidate, not a takeover of arbitrary predecessor state. Do not silently disable/feed an armed watchdog. The exact safely accessible RAM and entry stub require final ELF/order proof.
3. While NOR is known available, copy/verify island text, literal/data tables, terminal and exception code. Establish internal-RAM operation and exception stacks and vector table. Mask configurable IRQs; RAM NMI/HardFault and relevant fault handlers remain terminal with no Flash logging/reset. FPU/lazy state and other bus masters must be excluded or explicitly established.
4. Only after readiness of that RAM context may any selected firmware FlexSPI clock/LUT/probe/mode change occur. Guard actual MEMC/NOR POST_KERNEL device initialization as well as later operation entry. Prefer verifying the already selected mode and refusing unapproved status writes. Existing callbacks into kernel/drivers, literal veneers, indirect calls and cache/barrier routines require transitive proof; placement of three driver objects alone is insufficient.
5. During NOR unavailability, no ordinary XIP code/data access. A qualified, independently progressing RAM-local elapsed timer must use the fitted NOR maxima plus margin. GPT2 remains only an option pending ownership/clock/reset proof; observation loops are not elapsed deadlines and a stalled MMIO access remains unresolved. Uncertain state stays in RAM; a timeout never authorises reset or XIP return.
6. Restore only after trustworthy ready/controller-idle confirmation. Restore cache/mapping, original vectors/stacks/IRQ state in a defined order, then return to ordinary XIP. Controller/watchdog arming follows successful maintenance exit.

Pinned source evidence: Zephyr `arch/arm/core/cortex_m/reset.S` calls `soc_reset_hook` before `z_prep_c`; `soc/nxp/imxrt/imxrt10xx/soc.c` calls `SystemInit` from that hook. NXP `system_MIMXRT1062.c` clears watchdog power-down counters before C data/relocation. `z_prep_c` relocates vectors and then calls `arch_data_copy`; `arch/common/xip.c` performs generated relocation copies there. `z_cstart` calls `soc_early_init_hook`/clock initialization before driver-init levels. Thus late main-only guards or ordinary relocation do not preserve inherited watchdog evidence.

A stackless candidate-only `--wrap=soc_reset_hook` snapshot before the real hook is the intended early evidence capture. Reserve a small NOLOAD slice of dedicated OCRAM2 `[0x20200000,0x20280000)`, not the FlexRAM window at `0x20280000`, and verify no stack or initialized-global dependence. Ordinary relocation already precedes clock/device initialization; do not move all copying before SystemInit without a demonstrated need. Known armed WDE/PDE/RTWDOG EN fields must cause refusal before the real hook can disable them. Apply candidate-only `DISABLE_WDOG=0` and verify actual SystemInit instructions; it removes RTWDOG service/disable but does not remove unconditional WMCR writes, which are admissible only when captured PDE was already clear. Before NOR mutation, an XIP refusal loop is distinct from the later RAM uncertainty terminal. Cache/MPU accessibility, external supervisor state and actual reset behaviour remain physical gates. Whole-application RAM would change ROM loader/copy/memory composition and would still need initial watchdog and reset evidence. It does not automatically remove this blocker, so no second implementation is justified now.

## Ownership, provisioning and recovery

Candidate regions only: firmware `[0,0x620000)`, LittleFS `[0x620000,0x7ef000)`, independent scratch `[0x7ef000,0x7f0000)`, NVS `[0x7f0000,0x800000)`. The sample overlay and software fences must agree; production partitions remain unchanged. Owner-specific checks are required at composition boundaries; a generic address fence does not authenticate callers. Preserve firmware/header/load image and external programmer erase boundaries separately.

Initial provisioning is explicit and separately authorised, following identity/content inspection and backup. It may erase only the named data owner, explicitly format LittleFS and create NVS empty metadata/count. Close provisioning permission before normal readiness and never autoformat after failure. Scratch is not a filesystem block and is reserved even after first-write testing.

Permit normal LittleFS/NVS garbage collection; do not add a predictive storage-management layer. Exercise a representative real-engine reclamation path and the provision → two-key/four-save → normal backend remount/readback path. A healthy NVS mount may write on some states; qualify only observed supported cases and continue to refuse uncertain repair/corrupt state without silently allowing all mount mutations. Persistent gate faults remain fail-closed. These tests do not establish arbitrary interrupted-write/power-loss recovery or whole-media integrity.

Use `report_every_cycles` U32/schema1/range1–100 and `diagnostics_enabled` BOOL/schema1, four scalar saves total in the trace. This is test data, not maximum capacity. Existing native API-count comparison (45→85 reads, 24 writes, zero erases in that trace) is retained without further optimization. Actual maintenance duration awaits separately authorised hardware.

## Implemented conditional read-only slices

The candidate now captures pre-SystemInit state, separates scratch ownership,
and checks the combined maintenance lifecycle on actual simulated engines.
Its later read-only harness copies/compares ITCM context code and DTCM vectors,
uses separate internal stacks and RAM terminal handlers, and always refuses
native Flash init/read/write/erase. It does not exercise program/erase busy
paths. A pre-clock wrapper conservatively refuses FlexSPI selector 0; the
selected configuration leaves serial-clock sources for selectors 1–3 unchanged.
This is not general clock, AHB, SDRAM, DCDC or predecessor qualification.
See the [verification and remaining gates](evidence/storage-maintenance/verification.md).

## Current approval gates

Software checks may establish lifecycle, four-owner geometry, negative request dispatch, representative GC/remount and exact ARM placement. They cannot establish initial hardware/watchdog state, fitted NOR timing, clock progress, other-master quiescence or safe target reset. Physical verdict remains **NOT READY** until the exact operational image, complete startup/transitive path, state evidence, provisioning and first-write/recovery procedure are independently reviewed and explicitly approved.
