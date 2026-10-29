# Multi-backend Logging SPIKE — technical assessment v1.0

**Issue:** [#103 — Evaluate configurable multi-backend logging support](https://github.com/wonny-labs/crux-pilot/issues/103)  
**Status:** Investigation and recommendation, **not** an approved implementation, production profile or hardware operation  
**Date:** 29 October 2026  
**Baseline:** Crux `main` as inspected for #103 and the pinned Zephyr 4.4.0 source at `684c9e8f32e4373a21098559f748f06915f950c9`.

## Decision summary

Use **Zephyr's existing deferred Logging core and native backends** to fan out diagnostics. Preserve the existing public `<crux/logging.h>` declarations, `crux::logging` consumer target, bounded completed-text format, common severity threshold and best-effort submission semantics. Keep backend selection and native readiness policy in **board/project composition**, not in portable applications.

Recommend **UART and UDP Syslog as the first configurable output profile** under [#110](https://github.com/wonny-labs/crux-pilot/issues/110). Treat RTT as an optional debug transport requiring separate full-record and debugger qualification under [#111](https://github.com/wonny-labs/crux-pilot/issues/111). Defer File Logging integration under [#112](https://github.com/wonny-labs/crux-pilot/issues/112) until the storage approval gates are released. Do **not** qualify TCP Syslog in the initial profile because the pinned native network backend has blocking transport paths and shares Zephyr's log processing thread.

No Crux-specific backend registry, additional queue, worker, dispatcher, public per-backend selector, delivery acknowledgement or new OS-independent logging framework is justified by the evidence.

## Current Crux baseline and constraints

- [The approved Logging contract](logging-contract.md) exposes `crux_log_write(level, tag, text)` and `crux_log_set_level(maximum_level)`. Valid tags contain 1–24 ASCII bytes and completed text contains 0–256 bytes. A successful call indicates validation/filtering or an attempted native submission, **not** backend admission, transmission or persistence.
- The [current Zephyr provider](../components/logging/ports/zephyr/logging_zephyr.c) checks the readiness of **one selected UART** before any native `LOG_*` submission. Its [binding checks](../components/logging/ports/zephyr/logging_binding.c) require `zephyr,log-uart` and reject Serial/Console/Shell/other incompatible owners.
- [Zephyr Logging CMake](../components/logging/ports/zephyr/CMakeLists.txt) rejects RTT, NET, FS and other additional native backends, as well as native runtime filtering. This is an intentional safety boundary for the initial UART-only profile, **not** proof that the native logging core lacks multi-backend support.
- The accepted initial EVKB Rev B UART composition uses LPUART1 at 115200 8N1, with a narrowly qualified inert `UART_CONSOLE=y` prerequisite and direct Console input/output disabled. Separate Serial LPUART3 can be used. These conditions and [#29](https://github.com/wonny-labs/crux-pilot/issues/29) M5 physical acceptance are unchanged.
- Linux remains at its currently supported synchronous stderr Host-provider scope.

## Capability and feasibility assessment

| Native backend | Prerequisites and configuration | Observed or source-proven behaviour | Recommendation |
| --- | --- | --- | --- |
| UART | `LOG_BACKEND_UART`, `UART_CONSOLE`, enabled explicitly chosen UART and exclusive resource ownership | Existing qualified software tests exercise the UART emulator. Actual EVKB output still requires the separate M5 physical acceptance | Retain unchanged baseline and select independently |
| SEGGER RTT | Pinned `segger` west module, `USE_SEGGER_RTT`, `HAS_SEGGER_RTT`, suitable debug probe and buffer configuration | EVKB Rev B **cross-build passes** with RTT Drop mode. Pinned Drop-mode message buffer is limited to **256 bytes**, smaller than a fully rendered maximum Crux record including its tag and native prefix. Block/Overwrite modes have their own latency/integrity trade-offs | Optional debug-only until record length, output mode, debugger absent and physical capture are qualified |
| UDP Syslog | `NETWORKING`, `NET_SOCKETS`, `NET_UDP`, configured IPv4/IPv6 route and server, suitable backend activation after readiness | Native `zsock_sendmsg` requests `ZSOCK_MSG_DONTWAIT` for UDP. An actual `native_sim` Zephyr-loopback fixture verifies UART and UDP fan-out and UART output during a simulated network-down episode. A successful send is not remote receipt confirmation | Preferred initial Ethernet backend |
| TCP Syslog | `NET_TCP`, server URI with `tcp://`, socket context and peer availability | Native `zsock_connect` and TCP `zsock_sendmsg` have no bounded nonblocking policy in this backend. A stalled peer may delay the shared logger and all other outputs. A `tcp://` server with `NET_TCP=n` follows a source-proven fallback path to UDP | Do not enable in the first profile. Require a separate transport/latency decision before considering support |
| File | `FILE_SYSTEM`, mounted approved volume and a chosen log directory, file size/count/overwrite policy | Native FS backend supports bounded file count/size and rotation. Missing mount, full storage, write failures and backend corruption can discard records without affecting `crux_log_write` return values. Flush notification is not a per-record durability guarantee | Defer to approved storage and additional sync/recovery qualification |

**RTT message integrity:** On the pinned source, Drop mode stages characters in `CONFIG_LOG_BACKEND_RTT_MESSAGE_SIZE` (range 32–256). Characters beyond the available line buffer can be omitted before newline. The 256-byte Crux **text alone** can exhaust that limit before the tag, severity and native metadata are rendered. Overwrite mode can discard older bytes and Block mode can stall the common logger when a debugger is absent or slow. An RTT cross-build alone cannot establish a safe full-record profile.

**Syslog transport and privacy:** The pinned network backend emits RFC 5424-style diagnostics over plain UDP or TCP. It is neither authenticated nor encrypted by that backend. Deploy only within an appropriately restricted diagnostic network unless a separately approved secure transport is introduced. The native output buffer must be configured large enough for the **complete rendered** bounded record, not just the 256-byte Crux text.

## Actual software evidence and its limits

The isolated test code is in [Draft PR #109](https://github.com/wonny-labs/crux-pilot/pull/109), under `tests/spike_logging_103/`. It deliberately uses **native Zephyr Logging with `CONFIG_CRUX_LOGGING=n`**, since current production Crux guards reject the investigated combinations. The tests do not claim that portable Crux multi-backend integration has already been implemented.

| Evidence | Result | What it does not establish |
| --- | --- | --- |
| [Zephyr CI 40902091482](https://github.com/wonny-labs/crux-pilot/actions/runs/40902091482) at `0259d3f` | Host, Zephyr and Docs CI all PASS, including actual native_sim UART + FS 1/1 and EVKB RTT cross-build | Production Crux backend selection or physical output |
| [Native UART + FS evidence run 40901027729](https://github.com/wonny-labs/crux-pilot/actions/runs/40901027729) | Actual `SPIKE103_NATIVE_RESULT uart=1 fs_write=1 fs_marker=1 fs_sync=0`. Both UART and FS received one message. No `fs_sync` was observed in the following 50 ms | That unsynced data is necessarily lost, or that all multi-record and power-cut cases behave the same |
| [Native UDP addition in PR #109](https://github.com/wonny-labs/crux-pilot/pull/109) | Dedicated `native_sim/native/64` fixture using an actual Zephyr UDP loopback receiver and UART emulator. The SPIKE CI step passed in the corresponding change run | EVKB Ethernet traffic, all network faults, WAN reachability or delivery guarantees |
| [Pinned Zephyr Logging core](https://github.com/zephyrproject-rtos/zephyr/blob/684c9e8f32e4373a21098559f748f06915f950c9/subsys/logging/log_core.c) | Native messages are processed sequentially for each active backend through the **shared** deferred logging processing thread | Independent per-backend latency, ordering or loss isolation |
| [Pinned native backend sources](https://github.com/zephyrproject-rtos/zephyr/tree/684c9e8f32e4373a21098559f748f06915f950c9/subsys/logging/backends) | UART, RTT, network and file capability, activation, formatting and failure paths examined | Tests of every backend combination or a supported Crux product configuration |

The test-only simulated LittleFS partition is 64 KiB. It is not the target's QSPI layout. No physical board was connected, reset, programmed, erased or used as a logger. The existing M4 Flash safety and ownership decisions remain in force.

### Why the File sync observation matters

In the pinned core, `log_process()` can return false after consuming the only pending message. Its processing thread sets `processed_any` only when `log_process()` returns true and calls `LOG_BACKEND_EVT_PROCESS_THREAD_DONE` only after a later false result. The native FS backend calls `fs_sync()` in that notification. For an isolated final message, the notification therefore need not occur. This was observed in the real pinned `native_sim` fixture as `fs_write=1` and `fs_sync=0`. A proposed file backend must address that condition or narrowly document an independently demonstrated persistence policy before claiming durable logs.

## Recommended configuration ownership

| Responsibility | Selected mechanism | Required rule |
| --- | --- | --- |
| Backend inclusion | Native `CONFIG_LOG_BACKEND_UART/RTT/NET/FS` with project Kconfig fragments | Enable each backend independently, permit only qualified combinations, reject an enabled Crux Logging profile with no supported output |
| UART resource and ownership | Devicetree `zephyr,log-uart`, overlay, current static Crux binding checks | Require UART bindings **only** when UART is selected. Preserve all UART ownership and Console exception safeguards |
| Native dependency and capability checks | Zephyr Kconfig plus focused Crux CMake/configuration validation | Reject silently disabled requested backends, unavailable SEGGER module, missing network prerequisites, invalid `tcp://`+TCP-disabled cases and unqualified profiles |
| Startup/lifecycle | Native Zephyr backend activation plus trusted platform composition | UART may autostart. Network activation must follow interface/address/server readiness. File output must follow authorised volume readiness. A runtime outage is not a build-configuration error |
| Global Crux severity | Existing common `crux_log_set_level()` | Keep its current boot-local filter semantics. It is not a per-output, per-tag or persistent policy |
| Per-backend severity | Prefer fixed native compiled limits initially | Add `CONFIG_LOG_RUNTIME_FILTERING` and controlled `log_filter_set()` only with an evidenced requirement and tests. `crux_logging` is one native source, so native filtering cannot distinguish portable Crux tags without a new mechanism |
| Output and loss reporting | Zephyr native record formatting, queue and backend diagnostics | Continue best-effort `CRUX_OK` semantics. Do not add a delivery receipt, public flush, retry or separate dispatcher in the first implementation |

**Minimum Crux changes for #110:** Relax the currently universal backend rejection in `components/logging/ports/zephyr/CMakeLists.txt` only for qualified selections. Make `logging_binding.c` UART-specific instead of unconditional. Replace the UART-only readiness assumption in `logging_zephyr.c` with a selected-backend-aware policy that retains existing UART-only `NOT_READY` behaviour where applicable. Keep public API and shared validation unchanged. Add focused positive and negative configuration checks and a source-identical consumer test. Do not weaken the established UART-only evidence or Console/Serial conflict guards.

## Timing, loss, resource and recovery assessment

All selected outputs share the native deferred log buffer and processing thread. A slow or blocked backend can delay the others **after** submission. Drop-new queue policy and its aggregate loss report remain appropriate for noncritical diagnostics. `CRUX_OK` cannot guarantee that any particular backend accepted, transmitted or retained the record. A valid log should not become a watchdog dependency, a control-loop deadline guarantee or the sole evidence of safety-critical faults.

- **UART:** Polling output is subject to baud rate, device readiness, console ownership and aggregate queue pressure. Maintain the existing exclusive LPUART1 profile until an explicit sharing combination is qualified.
- **RTT:** A debugger may not be connected or reading. Block mode risks stalling the shared logger. Drop mode has a 256-byte rendered-message limitation in this pin. Overwrite mode can compromise old record integrity. These are separate trade-offs, not equivalent reliable output modes.
- **UDP:** Nonblocking send limits the socket-send wait but does not imply no cost from socket creation/connection, packet allocation or stack scheduling. Network down/unreachable may silently lose a datagram. Neither automatic retry nor delivery confirmation exists. A repeated failed connect can consume processing time.
- **TCP:** Blocking connect and send can stall the common processing thread, including UART and RTT fan-out, and may retain a socket without automatic reconnect after a later failure. A new Crux worker would be a substantial architectural cost and requires evidence, not a default response.
- **File:** Mounted-volume availability, free space, overwrite/reclamation, sync cadence and operation cost can introduce long stalls or data loss. The native backend can silently discard records on failure. Rotation size/count must be selected against usable capacity and write/erase endurance. File logs must not share unapproved Flash regions with firmware, Settings or other application files.
- **Footprint:** The original UART-only Zephyr qualification used a 4096-byte shared logger buffer and a 2048-byte configured logger stack (2112-byte target stack allocation including guards/alignment). The native UART+FS SPIKE uses a 4096-byte logger stack, a 4096-byte logger buffer and a native 256-byte FS output buffer. The native RTT build configures a 1024-byte RTT up-buffer and a 256-byte Drop-mode line bound. The native UDP fixture requests a 512-byte Syslog formatting buffer and also consumes Zephyr sockets, packets and network buffers. These are **configuration allocations**, not measured incremental target RAM/Flash deltas, runtime peaks, WCET or production sizing.

Follow-up target validation should compare **identical application source** across selected backend compositions and retain generated `.config`, DTS, ELF/map, stack observations and actual UART/UDP capture. Check representative bounded bursts, network-down, slow-output, full buffers and backend unavailable cases. Document measured loss and latency without imposing a fake whole-call worst-case bound. Do not repeat full unrelated component test matrices.

## Prioritised implementation and deferrals

| Priority | Work item | Decision and gate |
| --- | --- | --- |
| 1 | [#110 — Support configurable UART and UDP Syslog logging](https://github.com/wonny-labs/crux-pilot/issues/110) | Implement first after adoption of this assessment. Coordinate static selection with [#32](https://github.com/wonny-labs/crux-pilot/issues/32). Preserve M5 [#29](https://github.com/wonny-labs/crux-pilot/issues/29) unchanged |
| 2 | [#111 — Qualify SEGGER RTT as an optional logging backend](https://github.com/wonny-labs/crux-pilot/issues/111) | Proceed only after selecting an acceptable nonblocking/full-record policy and obtaining specific physical probe approval if actual target support is claimed |
| Deferred | [#112 — Enable bounded file logging on approved storage](https://github.com/wonny-labs/crux-pilot/issues/112) | Blocked by [#14](https://github.com/wonny-labs/crux-pilot/issues/14)–[#16](https://github.com/wonny-labs/crux-pilot/issues/16) storage safety, owner, persistence and hardware decision gates |
| Not in initial plan | TCP Syslog | Reconsider only if a concrete consumer requirement justifies its shared-thread blocking and recovery risks. A separate scoped experiment/design decision must precede support |

## Acceptance criteria coverage and remaining decisions

| #103 criterion | Assessment outcome |
| --- | --- |
| Capability and feasibility for UART, RTT, UDP/TCP and FS | Addressed by source-backed matrix, actual UART/FS and UART/UDP native_sim, RTT target cross-build and explicit unsupported physical/transport limits |
| Configurable independent outputs and build/runtime/filter decisions | Proposed build-time/native selection, lifecycle responsibilities and conditional backend filters. **Not yet implemented** in Crux |
| Resource, blocking, loss and failure modes | Addressed qualitatively and with representative concrete buffer allocations and actual FS sync/UDP fan-out evidence. Target WCET and incremental memory are deliberately **unqualified** |
| Minimum Crux change and prioritised follow-up issues | Concrete source boundaries and issues #110–#112 identified without modifying #29 acceptance |

**Human decision before adoption:** Approve the UART+UDP-first direction, accept RTT/File/TCP deferrals and the retained best-effort data-loss semantics. A technical review of the source and evidence, including a separate independent Reviewer/Challenger as required by the active workflow, remains a review gate. This report does not authorise implementation, issue closure, a PR merge, physical Flash operations or additional supported product backends.
