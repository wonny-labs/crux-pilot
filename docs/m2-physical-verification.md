# M2 physical verification plan

Authorisation: user explicitly permits current-task Flash, Reset and UART verification for PR #9. This supersedes the earlier software-only hardware prohibition for this task. No PR merging, issue closure, M2 completion declaration, fuse/security/boot-layout operations, full-chip erase, board rework or M3 functionality is authorised.

## Inputs and preflight

- Reuse the previously user-confirmed MIMXRT1060-EVKB PCB B, MIMXRT1062, standard QSPI 8 MiB, SW4 OFF–OFF–ON–OFF, no HyperFlash rework, external J-Link PLUS 602003106 and J41 UART cu.usbmodem11102/115200. Current USB serials must match; connect must identify Cortex-M7 and plausible 3.3 V VTref. PCB/switch identity is the retained physical setup, not something a debugger can remeasure.
- Confirm no active known debugger/serial process, acquire exclusive UART via pyserial+TIOCEXCL and probe by serial, keep exclusive UART during programming/reset/capture. Stop on identity/voltage/ownership mismatch. A hung lsof inventory is not evidence of exclusivity; OS-enforced UART and J-Link connection checks are required.
- Use clean, frozen source and pinned Zephyr 684c9e8f / SDK 1.0.1. Record source/dependency/configuration identity, ELF/BIN/config/map hashes, build commands/logs and firmware footprint. Prefer fresh builds; a previously evidenced firmware must retain its actual revision and independently verified compiled-input equivalence, never be relabelled as a newer build.
- Program only the named selected image to its established QSPI location through the existing SEGGER loader, no raw erase command. Verify programmed sections with SDK GDB (rom_start/text/rodata) or, if using Commander BIN loading, full BIN at its established link address. Save programming and verification logs. Release GDB Server before Commander reset, keeping UART exclusive.

## Native execution image, then reference controller

1. Native execution tests: same selected-native probe source/config as investigation, on EVKB B/QSPI. Three fresh resets, capture 20 s each maximum. Require suite 4/4, no failed/error cases, final PROJECT EXECUTION SUCCESSFUL. Check six 10 ms monotonic releases with no early deadline assertions; deliberate 25 ms work skips>=2 phase points; lifecycle join/static reuse, native PI/mutex contention+expiry+recovery, delayed semaphore wake/expiry/capacity, bounded message delivery/full/empty/expiry. Record printed lateness/skips/stack and distinguish results from the controller 100 ms scenario. No unselected POSIX image required; physical POSIX remains unverified.
2. Controller: normal current 20-observation / 100 ms composition, three fresh resets. Capture bound 15 s per boot (liveness only), native worker join 10 s. Require init 0, cycles 0..19 exactly once, cycles 0–1 NO_DATA/unavailable; cycles 2..19 OK/numericUTC; cycle 6 UTC lower than 5 after explicit backward set; increasing uptime and non-negative lateness; no failure marker; completion only after worker join and successful stack accounting. Record intervals/lateness and each skipped-count observation. Unit tests of the shared first-future schedule plus deliberate native-target overrun probe support the policy; do not claim a forced overrun in this normal controller run. If that distinction leaves a material gap, add only a controlled test variant after review, not a new execution feature.
3. Resource acceptance objective fixed before execution: for these short unloaded runs the configured 4096-byte worker must retain at least 1024 untouched bytes at completion (25% diagnostic headroom), with successful accounting and no detected fatal/overflow outcome. This is a conservative scoped validation objective, not a derived worst-case stack bound or production guarantee. Record measured used/unused bytes and ratio; do not infer target stack use from native_sim. Record static RAM/Flash, priority 5, configured heap 0 and allocation assumptions; no dynamic controller allocation claim beyond inspected code/config.
4. Do not repeat unchanged physical Time contract/range tests. Retain appropriate Host/native_sim init/read-error recovery evidence; do not inject electrical faults, remove RTC or change board wiring. No long-run, concurrent-load, jitter upper-bound, drift, ISR or M5 guarantee is claimed. Timing capture uses kernel monotonic values; UART host timestamps measure transport/liveness only.
5. Preserve original failures and logs. Sanitizer process-exit timeout remains an unresolved observation; this hardware test cannot resolve it. Final hand-off leaves the normal identified controller image, run complete, debugger/UART released.

## Evidence and acceptance gate

Store immutable per-image/per-run raw UART, host receive times, reset/program/verification logs, firmware/input/config hashes and parsed results under ignored build/evidence/m2-physical; retain a portable archive and SHA256 index. Add a durable scoped result/failure/limitations summary to PR #9, independent reviewer inspects raw evidence/config/source/provenance before PASS. Keep PR Draft and issues/M2 open for final human review/integration even if required physical evidence passes. Unexecuted or failed cases remain explicit. No claim that physical acceptance validates POSIX, resolves ASan timeout or approves a platform standard.

## Reproduction and preparation status

The maintained macOS runner is `tests/target/capture_execution.py`. It requires explicit
`--program --mode native|controller --uart /dev/cu.usbmodem11102 --probe 602003106
--board mimxrt1060_evk@B//qspi --manifest <identified-image.json> --evidence <new-output-directory>`.
It validates manifest artefact hashes, acquires exclusive UART, checks probe/CPU/voltage,
programs and verifies the full BIN, then resets/captures three runs. It does not erase
the full chip. The manifest must establish actual ELF load-segment/BIN agreement,
`rom_start` at the BIN base and the verified QSPI range before this command.

The local Linux container could not restart during preparation, although Docker's daemon
remained responsive. No new local firmware build was accepted from that attempt. Use
completed pinned hosted builds with retained native/controller artefacts instead. Existing
software source remains unchanged; the workflow now retains native ELF/BIN/devicetree
for identified physical verification as well as the earlier maps/configurations.

An initial broad macOS unittest invocation included Linux-only provenance/build safeguards
and failed on macOS path aliases and explicit Linux guards. Those failures are retained
and are not a firmware defect or a passing Linux suite. The focused physical-output
validator tests pass on macOS; the full supported Linux suite must pass in hosted CI.
The first Commander `q` preparation invocation did not connect to the target and was not
accepted as preflight. Explicit `connect` then identified probe 602003106, Cortex-M7 r1p1
and VTref 3.327 V with exclusive UART access, without programming an image.

## First native capture and oracle correction

The first actual `b6db696` native attempt (`native-01`) is **FAILED**, not qualifying
physical evidence. Programming and full 52,328-byte BIN readback succeeded, but Commander
reported `Could not start CPU core` on disconnect despite exit zero. An explicit later
reset/start produced a full four-case suite, preceded by stale partial output from the
pre-reset execution. The validator rejected that mixed capture; all bytes and logs remain.
This disconnect/start diagnostic is retained, not silently described as resolved.

The target also exposed a mistaken observer assumption: `sizeof(worker_stack)` is
**4160 bytes**, not the requested usable 4096. Pinned ARM `arch.h` selects a 64-byte MPU
guard when `CONFIG_MPU_STACK_GUARD=y` and region minimum is 32. With userspace/FPU disabled,
`K_THREAD_STACK_SIZEOF` maps to kernel-stack object size minus that reserved guard;
`thread.c` accounts the resulting 4096-byte usable buffer. Actual linker maps independently
show `0x1040` objects for both native worker and controller. The observed native unused
3840 bytes in the rejected run is diagnostic only; the original minimum 1024/4096
headroom objective is unchanged.

The capture-only correction preserves/drains pre-reset bytes until 200 ms silence
(with a five-second bound), then requires qualifying output to begin with exactly one
fresh boot. It never trims qualifying bytes. The physical parser distinguishes 4160
allocated bytes, 64 reserved bytes and 4096 usable bytes. Negative controls reject stale
prefixes and wrong object size. Firmware C/configuration and the identified BINs are
unchanged. Qualifying runs must use a committed clean capture revision, recorded
separately from the firmware revision, after independent review of this correction.

## Qualifying physical results

The separately authorised execution used firmware from clean
`b6db69673094213fb4df609191dce9aead895d4d`, built by completed
[Zephyr CI 39078171686](https://github.com/wonny-labs/crux-pilot/actions/runs/39078171686)
(artefact `12118111089`). Corresponding [Host CI 39078171692](https://github.com/wonny-labs/crux-pilot/actions/runs/39078171692)
passed Debug/sanitizer 34/34 each, 16 safeguards and configured quality checks. The
capture correction was committed cleanly at `afb51605be9dc46a81ef64326d33e71b8573da07`;
its twelve focused controls passed before retry. Subsequently, corrected-runner
[Host CI 39078980991](https://github.com/wonny-labs/crux-pilot/actions/runs/39078980991)
passed Debug/sanitizer 34/34 each, all 18 safeguards and configured quality checks.
Capture source and firmware source are
distinct identities: no firmware C, execution configuration or BIN changed for the retry.

| Image / condition | Qualifying result |
| --- | --- |
| Native execution probe | `native-02`: three fresh boots, four native cases each, all pass. Lifecycle/join/static reuse, mutex PI/contention/expiry/recovery, semaphore expiry/delayed wake/capacity and bounded messages are evidenced by the actual assertions. |
| Native periodic observation | Six 10 ms releases per boot; deliberate 25 ms work; 82 ms aggregate and two skipped phase points each run. Reported per-release millisecond lateness is zero. |
| Reference controller | `controller-01`: three fresh boots, 20 observations each; unset at 0–1, valid after 2, backward UTC set at 6; monotonic phase and final joined completion pass each run. |
| Controller timing observation | All 19 intervals per run are 100 ms at the recorded uptime resolution; reported lateness zero, skipped count zero. This normal scenario does not force an overrun. |
| Native worker stack | 4096 usable / 4160 allocated / 64 reserved bytes; 3840 untouched, 256 observed used in every qualifying boot. |
| Controller worker stack | 4096 usable / 4160 allocated / 64 reserved bytes; 3688 untouched, 408 observed used in every qualifying boot. Both exceed the unchanged 1024-byte short-run headroom objective. |
| Static image allocation | Native FLASH 52,328 B / RAM 17,412 B; controller FLASH 37,536 B / RAM 8,704 B. Priority 5 and configured heap zero; observed stack usage is not total system-stack or worst-case resource qualification. |
| Programming identity | Full BIN readback at ELF-verified `0x60000000` matched before every image's qualifying resets. Runtime preflight again identified probe 602003106, Cortex-M7 and VTref 3.322 V with exclusive UART. |

Native BIN SHA-256:
`59668e4e445e695c973f225a2a081a9c1278403ef76532ce8540da417cda4fc8`.
Controller BIN SHA-256:
`4be80bc7cfcfc116bebba08d3d9c56b347cc6452548da299f7bf09a446ed55d2`;
the controller BIN also matches the earlier software-evidence image, without claiming
identical debug ELFs. Independent image verification checked each ELF load segment,
actual `rom_start`, exact board/SoC/QSPI/guard configuration, clean pinned dependencies
and SDK 1.0.1 before programming.

All qualifying bytes begin with one fresh boot; pre-reset drain bytes are preserved
separately. The rejected `native-01` is not included in the qualifying counts. Its
stale capture and observer correction remain recorded above. Controller programming
also emitted the post-verify, post-quit `Could not start CPU core` diagnostic despite
exit zero. It is explicitly retained in inputs/logs; each subsequent explicit reset/start
had no such error and produced a complete fresh qualifying run. This establishes the
planned reset/start path, not an explanation or cure for the disconnect diagnostic.

The prior sanitizer process-exit timeout remains an **unresolved observation**. No
recurrence in these unrelated physical runs can establish its cause. Physical POSIX
behaviour remains unverified; no target POSIX performance comparison, concurrent-load,
long-duration, sub-millisecond jitter, ISR or production guarantee is claimed. Existing
Time/RTC contract evidence is reused; no electrical fault injection or M3 service was added.

The last programmed image is the normal controller and its final finite scenario has
completed. UART/probe processes were released. PR #9 remains Draft and unmerged; issues
#6/#7 and M2 remain open for human review/integration rather than automatic closure.

## Independent verdict and retained evidence

The independent reviewer returned **PASS for the predeclared selected-native physical
validation of PR #9**, after independently reconstructing all six qualifying raw UART
runs, receive events, reset/program logs, source/configuration and artefact hashes.
The reviewer authored no firmware/capture code and performed no hardware operation.
This verdict is bounded physical acceptance; current runner CI and publication accuracy
are separate checks, and final human integration/M2 decisions remain outstanding.

The portable evidence archive is `crux-m2-physical-b6db696-afb5160.tar.gz` (922,891 bytes),
SHA-256 `f9863c2f9f543950d0fcceee1d94180d3c3cc8e59f0674638367cf216ae8cdc3`.
Its internal `SHA256SUMS` covers 106 raw evidence/source/firmware/CI files, including
failed preparation and the actual independent physical review at that stage. A durable
copy is retained with this chat's output artefacts; ignored build copies remain under
`build/evidence/m2-physical`. Original manifest paths identify local source files; the
archive contains their matching bytes under `firmware/` and `ci/` for portable inspection.
Hosted artefacts have 14-day retention; this archive preserves the selected proof.
