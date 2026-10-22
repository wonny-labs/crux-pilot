# Isolated Flash safety remediation PoC

Experimental, software-only follow-up to [Issue #22](https://github.com/wonny-labs/crux-pilot/issues/22)
and the accepted [storage assessment](storage-configuration-assessment.md).
This is not an adopted Flash driver, partition layout, backend or production recovery policy.
**Do not program this image.** Its native driver initialization is also unqualified.

## Implemented experiment

The default build and pinned dependency checkouts are unchanged. The standalone
`samples/flash_safety_poc` composition generates three private source copies using
`scripts/prepare-flash-poc.py`, checks their exact upstream SHA-256 identities,
and substitutes them only in that composition's native library targets.
Upstream copyright/licence notices remain in generated copies. No copied third-party
implementation is committed. Source changes fail closed on an unknown baseline.

The transformation is experimental maintenance tooling, not a proposed upstream patch
format. Production distribution, target coverage and ownership require a separate decision.

- **NOR write/erase:** check WREN, page-program, each sector/block/chip erase and
  status-poll return. Stop further commands on the first error. Non-XIP errors
  return the native error; uncertain XIP errors enter the terminal hook before
  reset, IRQ unlock, cache invalidation or caller return. No partial write is
  reported as an all-or-nothing transaction.
- **NOR busy:** allow 1,024 busy-status observations before returning `-ETIMEDOUT`.
  A status error returns immediately. The XIP caller then enters the terminal hook.
- **MEMC/HAL polling:** private copies replace braced hardware-wait loops with
  1,024-iteration budgets. Exhaustion enters the terminal hook. Original conditions
  may be observed once more before budget exhaustion is checked. The progressing
  TX/RX data-size loops retain their original behavior; ordinary transfer length
  is not capped at 1,024 FIFO batches.
- **RAM data:** the private MEMC configuration is writable initialized data, placing
  `memc_flexspi_config_0` in SDRAM instead of Flash. The copied controller pointer
  can then resolve its register and AHB bases without that demonstrated Flash-data fetch.
- **Terminal:** the isolated hook is a nonreturning ITCM `nop`/back-branch loop,
  with no logger, timer, controller reset or ordinary return. It bounds the attempt
  to wait; it does not bound total execution time or provide operational recovery.

The 1,024 budget is a deliberately small experimental observation count. It is
**not a timeout derived from the Flash data sheet**, nor a wall-time deadline.
Individual MMIO/bus transactions could stall. Healthy hardware could exceed this
budget. No watchdog/reset behavior is approved by this PoC.

## Verification

Baseline source tree: accepted main `94b11e4`; Zephyr
`684c9e8f32e4373a21098559f748f06915f950c9`, NXP HAL
`2c2f28ac333e995d7279777409817c4b4f92c1ec`.
Linux amd64 development image, SDK 1.0.1/GNU Arm 14.3.0, network-disabled
containers, read-only upstream/cache mounts and no device exposure were used.

In the documented Linux environment, with `ZEPHYR_BASE` and SDK configured:

```sh
scripts/test-flash-poc.sh
```

The recorded build used the same west command with an isolated output directory
`/evidence/zephyr-poc2`. Existing Host regression configure/build/CTest were run
separately with the cached GoogleTest source and disconnected fetching.
The complete convenience script above was also run successfully with
`CRUX_FLASH_POC_BUILD=/evidence/zephyr-poc2`; its final ELF hash matched the
reviewed artifact. This does not claim every developer environment was tested.

| Check | Verified result and limit |
| --- | --- |
| Controlled NOR faults | 68 combinations passed: five initial write/erase branches with success, WREN/command/status error and stuck busy, both XIP/non-XIP; three-command write/block requests failing on second operation; mixed sector/block/tail erase failing on operation 16 with work remaining. Exact command/poll/reset/unlock counts establish no continuation. RAM copy and cache branches execute using host stubs. |
| Lower-layer stuck waits | Three actual transformed function scenarios passed: MEMC idle, HAL reset idle and HAL reset-bit wait. Host terminal uses `longjmp` to observe the branch without hanging. FIFO and command-done loops are transformed and cross-compiled but are not executed by this small harness. |
| Generator guard | Wrong NOR source rejected; original TX/RX progress loops preserved. Source-pinned inputs protect the intentionally narrow transformations. |
| Zephyr cross-build | Standalone EVKB B QSPI build passed; generated config/linker and final ELF produced. This is compilation only. |
| Selected placement | [ELF record](evidence/flash-safety-poc/placement.json): terminal `0x227c` (Thumb symbol `0x227d`); NOR/MEMC/HAL selected routines in ITCM; MEMC config `0x800000ec` in SDRAM. [Selected disassembly](evidence/flash-safety-poc/selected-disassembly.txt) shows the terminal has only `nop` and a local back-branch. |
| Existing Host regressions | Configure/build passed; [CTest 42/42 passed](evidence/flash-safety-poc/host-ctest.txt). No existing production behavior was changed. |

[Source hashes](evidence/flash-safety-poc/source-hashes.json) identify original and
generated source bytes. [Fault test output](evidence/flash-safety-poc/fault-tests.txt)
records the Linux run. Host extraction stubs device effects and is not a full
Zephyr runtime or hardware-register simulator. `-Wno-sign-compare` applies only
to this native-function harness because original driver range arithmetic mixes
off_t with device size; it changes no production warning policy.

TDD evidence retained locally under `build/flash-safety-poc` records initial
false-success/stuck-busy failures and the progressing-loop regression. Independent
review exposed missing late-operation cases and overly broad loop instrumentation;
these were corrected. A first build attempt failed from duplicate Crux module
registration; explicit module selection corrected it. The first successful ELF
still placed terminal in Flash because application objects were outside the
relocation scanner's Zephyr object search. Moving the terminal compilation into
the MEMC library fixed the final ELF. Build success alone was not accepted as proof.

## Remaining risks and approval gates

This PoC does **not** establish complete RAM/ITCM closure. Vectors, NMI/fault paths,
startup/probe LUT/clock failures, indirect calls/constants, cache/bus-master/DMA
ownership and all recovery paths remain physical-write blockers. The native
probe's deliberate startup terminal loops are not converted into a new boot policy.
The actual terminal loop could be interrupted by NMI/fault or an enabled watchdog;
its ITCM location alone does not make those effects safe.

A production solution requires an audited advancing time source and suitable
part-specific limits, coverage of every reachable wait including errors during
cleanup, and explicit ready/restored versus uncertain-state handling. It must
qualify initialized RAM accessibility, MPU/cache attributes and code/data lifetime.
Changing an initializer to RAM removes one demonstrated dependency; it proves
neither all dependencies nor memory retention during controller reconfiguration.

Firmware-growth, all Flash LMA/BIN/boot-header bounds and programmer erase policy
remain separate protections. No layout is adopted or changed here. No filesystem,
Settings, format, destructive fault injection or persistence integration is added.

Before physical validation, separately approve remediation ownership and final
policy, exact source/configuration, board/part/probe identity, image/erase limits,
existing-data backup, sacrificial sector, operation/abort/recovery conditions,
exclusive ownership and verified code/data/exception/bus-master closure. Unknown
Flash contents are not permission to erase. No hardware/reset/merge/Issue-closure
permission follows from software tests or this Draft PR.

## Independent review

Separate Reviewer `/root/flash_probe_review` returned **PASS for the limited
software PoC**, independently checking current source transformations, late-error
tests, ELF bytes/configuration pointer chain and retained build/Host evidence.
Earlier REVISE findings were corrected and re-reviewed. One macOS lower-harness
run exceeded its 0.3-second process-launch/scheduling limit and passed immediately
on rerun; external containment was increased to two seconds without changing
the iteration-budget checks or candidate driver budgets. This review
is not production, physical-safety or Issue-closure approval.

## Retention and verification gate

Retain this opt-in experiment on `main` as a reproducible source-level regression
and review fixture for the accepted storage investigation. Its value is retaining
failure-path and placement checks against the pinned baseline, not supplying a
supported storage driver. Default compositions remain unchanged. Reassess or retire
it when an approved native remediation supersedes it; do not grow it into a second
production Flash stack.

The existing Zephyr workflow now runs the focused Host fault harness after fetching
the pinned sources. It reuses the existing C compiler, checkout and job: no extra
runner, dependency checkout or target build is introduced. Standard Host CI does
not have the Zephyr/HAL inputs and does not run this harness.

The full PoC target build and ELF tests remain a **documented manual merge gate**,
not an automated GitHub required check or branch-protection rule. For a PR changing
the generator, sample, test harness, verifier or pinned native inputs:

1. Run `scripts/test-flash-poc.sh` in the documented Linux environment, with the
   pinned workspace and SDK configured. Retain the full command output, input
   hashes, generated configuration/linker/map, ELF/BIN identity and selected
   disassembly. It must pass the fault harness, target build and three ELF tests.
2. Run applicable existing regressions and formatting/dependency checks; report
   skipped checks and failures explicitly. Software-only results qualify no board.
3. Independently review material source changes and verify that retained evidence
   matches the final implementation. If only documentation or CI wiring changes,
   confirm the PoC source/input hashes are unchanged instead of rebuilding an
   identical image. Identify the original tested source revision and latest head
   separately; do not re-label old evidence as a new run.
4. Before human merge acceptance, recheck **both push and PR workflow results on
   the latest head**. A green standard CI result alone does not satisfy the full
   manual PoC gate.

For this PR, the retained local implementation evidence is from `10b32a7` (final
ELF hash in the placement record). The follow-up adds CI wiring and review/gate
records without changing the generator, native input hashes, PoC sample or verifier.
A GCC CI failure also required expanding a compact `if` statement in generated
host-test C into braces and separate lines. This changes no test assertions,
PoC driver behavior or warning policy. The complete manual gate was rerun with
GCC and the fault harness with Clang; the target ELF remains unchanged. [Review provenance and resolved findings](evidence/flash-safety-poc/review-record.md)
separate the local subagent review from the later GitHub technical review. Neither
is a production-safety approval or authorisation to merge.
