# Storage Pilot software handoff

**Historical software handoff — superseded for M4 storage selection.** The QSPI NOR/LittleFS and separate Settings/NVS preference below is no longer the selected Pilot storage direction. See the [30 October 2026 decision in Issue #14](https://github.com/wonny-labs/crux-pilot/issues/14) for the microSD/FatFs File path and conditional Settings file-backed evaluation. Prior simulator results and safety findings remain valid only within their recorded scope.

Human approval on 24 October 2026 covers D1–D4 software implementation,
verification, independent review and Draft PR preparation. It does not adopt
production patches/partitions or authorise hardware, PR merge or Issue closure.
PR #51's validation method was accepted for this work; its repository merge
remains a separate action. Existing File and BOOL/U32 Configuration contracts
are unchanged.

## Delivered software candidates

- [File VFS/LittleFS simulator provider](file-native-validation.md): explicit
  private root binding, sleepable serialization, checked counts/offsets and
  truthful close/truncate lifecycle. Only native_sim is selectable.
- [Configuration Settings/NVS simulator provider](configuration-native-validation.md):
  full-record CRC reads, separate callback/traversal error checks, confirmed
  absence, and validation before activation. Explicit isolated opt-in only.
- Focused actual-engine failure/recovery cases reuse #44–#46; no universal
  power-loss, lifetime, timing or production capacity claim is added.

## Native patch maintenance and adoption gate

The Configuration fixture stages copies of the exact pinned Zephyr inputs,
never patches the upstream checkout. It currently reuses both #44 Settings
patches, the #45 initial comparison patch and one additional late-mount comparison
check. The retained manifests identify their exact hashes. These full experimental
patches are integration inputs, not automatic adoption of every changed path.
Before any target/downstream adoption, review/narrow the exact patch set to the
selected reachable API paths, preserve healthy behavior and assess compatibility.
Do not infer that suppressed ordinary registered `h_set` or unrelated lifecycle
errors are fixed by this direct-callback provider.

Crux Zephyr Native Integration owns any separately approved maintenance: pin/hash
checks, fault/healthy regressions, semantic review on updates and removal/backport
when appropriate upstream corrections are available. Upstream submission/sign-off
and patch adoption require their own human decisions; no contribution was sent.
Do not silently replace Settings with direct raw NVS or weaken error semantics.

## Remaining #14/#15/#16 gates

QSPI NOR + LittleFS with a separate CRC-enabled NVS region remains the preferred
design candidate. The #21 addresses and #46 first-ENOSPC counts remain proposals
and workload observations. Decide the Pilot file dataset, parameter/name budget,
headroom, save rate and exact medium/layout/ownership before target adoption.

No-format on LittleFS does not establish globally non-destructive initialization.
Settings/NVS may still repair/delete/write/erase metadata on other recovery
paths. Fail-closed handling of uncertain/corrupt startup and a controlled
maintenance recovery policy remain target-adoption blockers.

The pinned FlexSPI NOR errors, unbounded lower waits and safe busy termination
remain blockers. #35's software PoC is not the target driver. A target remediation
proposal must identify exact error returns and lower waits, keep uncertain busy
execution in safe RAM/ITCM without ordinary XIP return/reset, and provide the
final-image startup/code/data/vector/exception/IRQ/DMA/cache argument. Enforce
firmware/header/ELF-LMA/BIN bounds, runtime data-area fencing and actual programmer
erase preservation; #37's checker alone is not effective enforcement.

Only after those gates and separate human approval may an exclusive M4 board
session be arranged with M5. Supply exact image/hash/regions, fitted-part/current
data identity, backup, programmer behavior, limits and recovery/hand-back method.
Then verify public File/BOOL/U32 save/readback, one approved normal reboot and
representative timing/health. No physical busy power cuts or endurance are planned.

#23 software closeout remains supported by its accepted evidence. #14/#15/#16
and M4 remain incomplete; simulation cannot replace mandatory physical reboot
acceptance. No Issues or milestone are closed by these software candidates.

Integration baseline: refreshed against main `98f0510` after the simulator tests.
The delta from the tested `be0b252` baseline is Thread/Sync documentation and
header comments; all recorded provider/test input hashes remain unchanged.
Final formatting, native-free guards and whitespace checks passed after refresh.
