# Controlled Flash target candidate proposal

**Historical PR #59 candidate:** the later [maintenance profile](storage-maintenance-profile.md) adds a read-only context harness and native entry refusal. The measurements below retain their original image identity.

**NOT READY FOR PHYSICAL WRITE. Do not program or boot this image.** This is an
isolated software proposal beyond accepted PRs #35/#37. It adopts no production
driver, layout, watchdog/recovery policy or exception architecture. No hardware
operation was performed. Its dormant `main()` contains no application Flash
operation, but native initialization can program NOR status/configuration before
`main()`: QE enable issues WREN/status programming, and IS25 probing can issue
read-parameter programming. Dormant application code is not a non-destructive boot
policy. The program/erase range fence does not intercept those status commands.

## Exact candidate and maintenance boundary

`samples/flash_target_candidate` selects only `mimxrt1060_evk@B//qspi` and explicitly
uses the pinned Zephyr/NXP/CMSIS modules. The source generator in
`tests/native/flash_target_candidate/prepare.py` reuses the accepted PoC's exact
source-hash guards and runtime/lower-wait transformations, then applies named,
count-checked startup/error and range-check changes to private generated copies.
Unknown source identities or changed anchors fail generation. No upstream file,
accepted PoC file or default application composition is changed. Original notices
remain in generated sources. Generated sources and their manifest publish through
fresh temporary inodes and atomic replacement; an existing output symlink or
hardlink is never opened for writing.

The added NOR changes are:

- Probe accepts `-ENOTSUP` as the specific JEDEC fallback to SFDP; transport and
  identity errors do not continue probing. The pinned `check_jedec` default and
  intentional IS25 fallback return `-ENOTSUP`; device readiness `-ENODEV` belongs
  to init. Probe failures, including final device configuration, reach the terminal
  before reset, interrupt unlock or normal return in this XIP composition.
- Post-probe clock errors reach the terminal. Native init/IS25 comment-only spins
  use the same terminal hook. This conservatively stops uncertain state and is
  not an approved recovery policy.
- IS25 RDABR failures return immediately, preventing read-parameter programming
  based on an invalid response. Four-byte mode checks WREN; configuration propagates
  four-byte transport errors while retaining unsupported-method fallback, and
  propagates QE errors. JEDEC octal-enable errors propagate instead of returning
  success. These are tested actual helper bodies, not replacement implementations.
- Actual native write/erase entry points invoke the private
  `crux_pilot_flash_request_valid()` fence before device/AHB lookup, interrupt
  locking or WREN. Firmware, cross-owner requests, zero/invalid extents, unsupported
  erase alignment and chip erase are rejected with `-EACCES`. Parent-owned
  `tests/native/storage_target_guard` defines the experimental regions and tests
  the boundary arithmetic. Neither region labels nor this candidate adopt a layout.

Inherited transformations retain checked WREN/program/erase/status errors, stopping
subsequent commands; uncertain XIP failures do not reset/unlock/return. NOR status
polling and braced MEMC/HAL hardware waits have 1,024-observation budgets. Progressing
FIFO data-length loops are unchanged. These budgets do not establish elapsed-time
limits: hardware accesses can stall, and healthy part timings may exceed a budget.
The terminal is only a local ITCM `nop`/back-branch; it provides no recovery guarantee.

Private NOR/MEMC/HAL text relocates to ITCM; their data, BSS and rodata relocate to
DTCM. This includes the copied controller/configuration, LUTs and write buffer.
The SRAM vector option copies vectors into SDRAM. Networking, DMA, watchdog and
USB-device software selections are disabled, and FPU is disabled in the actual
configuration. These reduce dependencies but do not establish exception closure,
peripheral reset state or bus-master ownership.

The sample links `ASSERT(_flash_used <= 0x620000)` and its default build runs the
parent-owned final ELF/BIN correspondence/envelope/boot-header gate. This includes
Flash load bytes for relocated RAM sections. It does not restrict an external
programmer's erase policy or prove execution safety.

## Verification and retained identity

Baseline source: Crux `b31f4f0`; Zephyr
`684c9e8f32e4373a21098559f748f06915f950c9`; NXP HAL
`2c2f28ac333e995d7279777409817c4b4f92c1ec`. Build environment: Linux amd64,
SDK 1.0.1/GNU Arm 14.3.0, read-only upstream source/cache mounts, no target-device
exposure. This was compilation only.

Final ELF SHA-256: `c830176753fa28515d358365c3c9dba9c9d5b8c6055029d4a356f1629bfdb99c`.
Final BIN SHA-256: `3870a8ff65ba0d8b61b1539c6763088d538106de8f29845a97f2c10b3ae5a640`;
BIN image span: 39,552 bytes. Addresses, generated-source hashes and logs
are retained in the companion storage-target evidence record. The full ELF, BIN, map, configuration,
DTS, linker/copy outputs and disassembly remain together under the isolated
`/tmp/flash-target-candidate` artifact directory; the exact image hash must match
before interpreting addresses. No earlier PoC address is substituted for this image.

Fourteen native test methods pass on Linux. Actual pinned extracted bodies use
controlled device/register effects, not a complete Zephyr or peripheral simulation:

- Eight probe failure phases stop before reset/unlock/return; successful JEDEC and
  unsupported-to-SFDP paths remain available. Init's failed post-probe clock stops.
- Actual configuration, IS25, four-byte and JEDEC helpers exercise hidden-error
  propagation and unsupported-device semantics.
- Fifty initial runtime success/error cases and twelve second-operation errors
  cover single/multiple pages and sector/block erase; no subsequent command or
  ordinary cleanup follows an uncertain failure.
- Ten invalid native requests show zero AHB lookup, interrupt lock, WREN, command,
  poll, reset and unlock effects.
- Eleven actual HAL TX/RX/short-tail/command-done/pre-idle/post-idle success,
  error and budget cases supplement the three inherited MEMC/reset tests.
- Fake-input output symlink/hardlink fixtures preserve source and manifest victims.
  Unknown source rejection and unchanged FIFO progress loops are also checked.

RED logs retain the observed erroneous startup return/cleanup, hidden helper errors,
unfenced native requests and link-following publication. Initial target configuration
failed due to an undefined Ethernet option and dependency-selected printk/log-level
choices; the candidate now uses the pinned `ETH_DRIVER` option, disables the boot
banner and omits irrelevant log-level choices. The pinned DTS still emits its
USB host/device duplicate-unit-address warning. No new warning suppression is
applied to target code. Native extraction retains the accepted harness's scoped
sign-compare/unused-function allowances for stubbed interfaces.

```sh
ZEPHYR_BASE="$WORKSPACE/zephyr" python3 tests/native/flash_target_candidate/test_candidate.py
west build -b 'mimxrt1060_evk@B//qspi' -d /tmp/flash-target-candidate/build \
  "$CRUX_REPO/samples/flash_target_candidate" -- \
  "-DZEPHYR_MODULES=$CRUX_REPO;$WORKSPACE/modules/hal/nxp;$WORKSPACE/modules/hal/cmsis;$WORKSPACE/modules/hal/cmsis_6"
python3 tests/native/flash_target_candidate/assess_image.py \
  --elf /tmp/flash-target-candidate/build/zephyr/zephyr.elf \
  --config /tmp/flash-target-candidate/build/zephyr/.config \
  --output /tmp/flash-target-candidate/phase-assessment.json
# Expected assessment exit: 2, mandatory physical-write obligations remain.
```

## Final-image phase assessment and decisions

`assess_image.py` records the final image hash, named symbol/vector measurements,
actual configuration and a fail-closed phase assessment. It is an evidence report
for this candidate, not a general transitive closure verifier. A selected symbol
set or compiled image never produces a closure PASS.

| Phase | Evidence and remaining mandatory blocker |
| --- | --- |
| Startup/probe | Actual startup error paths are remediated and tested. Final image contains initialization-time configuration writes. Complete boot/copy/SDRAM/MPU/cache/clock/probe dependency and ordering proof remains incomplete. |
| Idle/entry/preparation | Configuration and buffers are DTCM. Flash-resident `memcpy` veneers remain; some copies precede commands while ready. Each reachable phase needs ordering/pointer proof rather than treating all Flash calls as equally hazardous or safe. |
| Program/erase busy | Named NOR/MEMC/HAL text is ITCM and driver data is DTCM. The final MEMC port-adjustment copy compiles inline. This removes demonstrated dependencies, but full code/data/literal/indirect-call and stack closure is unproved. |
| Completion | Checked status errors prevent ordinary cleanup. Trustworthy part-ready detection, all reset/cache/restoration paths and safe return still require proof against the fitted part and exact final composition. |
| Failure terminal | Terminal instruction loop is local ITCM. No elapsed-time, NMI/fault, watchdog, reset or power-loss safety follows from that placement. |
| Vectors/exceptions | RAM vector copying is enabled, but copied NMI/HardFault entries and their handler paths still target Flash. NMI `0x6000719b`, HardFault vector entry `0x60005189` and `z_arm_fault` `0x60005041` remain Flash-resident. Main/interrupt stacks are SDRAM. This is a concrete physical-write blocker. BASEPRI critical sections also leave higher-priority/zero-latency exceptions to close. |
| Bus masters/watchdog | Minimal software selections exclude networking/DMA/USB-device stacks. This does not prove inherited peripheral state or every bus master quiescent. `CONFIG_WATCHDOG=n` does not prove hardware watchdog disabled; watchdog reset may attempt Flash boot while NOR is busy. |

A first physical write requires a human decision on exception handling, master
ownership/quiescence, qualified deadlines/part identity, watchdog and uncertain-state
recovery, plus closure evidence for the resulting exact image. Whole-application
RAM execution or custom NMI/fault terminal handling are possible architectural
choices, not adopted by this candidate. Merely increasing observation budgets or
relocating selected handlers cannot satisfy those decisions. The separate
[first-write proposal](storage-target-first-write-proposal.md) retains the data,
backup, erase-preservation, scratch-sector and operation/abort/recovery gates.
