# UDP Syslog Logging Validation

This document records software evidence and its limits for Issue #110. It does
not qualify physical Ethernet operation or claim end-to-end Syslog delivery.

## Configuration contract

Crux Logging keeps the existing public API and uses Zephyr's deferred Logging
core and native backends. Select at least one of `CONFIG_LOG_BACKEND_UART` or
`CONFIG_LOG_BACKEND_NET`. UART-specific devicetree ownership checks apply only
when the UART backend is selected.

The UDP profile requires Zephyr Networking, UDP, sockets, the connection
manager, NET backend autostart, and the NET backend's connection-manager hook.
It also requires `CONFIG_LOG_BACKEND_NET_OUTPUT_TEXT=y`; MIPI SyS-T,
dictionary, and custom output modes do not produce the documented RFC 5424 text
records and are rejected by the Crux Zephyr composition check.
`CONFIG_LOG_BACKEND_NET_SERVER` must use Zephyr 4.4.0's numeric IPv4 or IPv6
syntax. IPv4 may include a UDP port; an IPv6 literal may be unbracketed when
using the default port 514, while brackets allow an explicit port. A bare IPv6
literal whose final hextet happens to look like a port remains an IPv6 address;
use `[address]:port` to specify an IPv6 port unambiguously. Explicit ports use
ASCII decimal digits and range from 1 through 65535. The address family must be
enabled by `CONFIG_NET_IPV4` or `CONFIG_NET_IPV6`; brackets are accepted only
for IPv6, scoped IPv6 literals are rejected, and `tcp://` selection is
rejected. DHCP-provided Syslog addresses are unsupported. The current feature
scope excludes TCP, RTT, and File Logging.

The common Crux filter and native severity mapping remain in force. The NET
backend formats RFC 5424 Syslog with the Zephyr local0 facility (facility 16),
so ERROR/WARN/INFO/DEBUG map to priorities `131/132/134/135`. UDP has no peer
acknowledgement. `CRUX_OK` reports that Crux submitted a valid record to the
native logger; it does not promise queue admission, socket success, collector
receipt, or persistence. Zephyr's native deferred logger remains the only
worker and queue; Crux adds neither.

## Failure and lifecycle limits

Zephyr 4.4.0's native NET backend lazily creates one socket when the deferred
logger first processes a record and uses `zsock_sendmsg` with
`ZSOCK_MSG_DONTWAIT` for UDP output. Its output callback returns the formatted
length even when the send fails. The failure is therefore not visible through
`crux_log_write` and does not cause socket recreation. With connection manager
support enabled, `L4-connected` activates the backend and `L4-disconnected`
deactivates it; the event handler does not close the socket, so reactivation
reuses the existing socket. This lifecycle description comes from Zephyr 4.4.0
source inspection, not descriptor-count instrumentation.

The current `native_sim/native/64` run passed the no-collector, simulated
L4-disconnect/reactivation, and active/inactive route-down cases in both UDP
profiles. The no-collector case closes the local receiver while the route is
up. UDP does not report that no process is listening, so this verifies
best-effort output and UART independence, not a sender-visible failure. For
route-down, the fixture verifies that direct UDP probe sends fail, submits
records while NET remains active, injects `L4-disconnected`, tests an inactive
burst, restores the simulated route, injects `L4-connected`, and checks recovery
delivery. The NET backend does not expose each send result to Crux or to this
test. In the dual profile, each route-down burst had a maximum observed
`crux_log_write` submission time of 0 ms at millisecond resolution, and the last
UART record was observed 20 ms after the last submission. UDP-only has no UART
observation. These are simulator observations, not WCET or production
logger-thread latency bounds. The tests inject L4 events and do not validate a
physical connection manager, physical route loss/recovery, or a target board.
The test performs one recovery cycle; it does not measure descriptor counts or
runtime resource peaks.

## Current verification record — 2026-10-30

The implementation and test revision is
`4100fd4b7633ce57ac72f4a771b6400b38cbf08f` on
`feat/issue-110-configurable-uart-udp`. Its working tree was clean in the
build-provenance captures. It is based on Zephyr
684c9e8f32e4373a21098559f748f06915f950c9; all three complete EVKB builds use
the same application, dependency revisions, source snapshot
`b5245c8d3575f70a03c1b3d97ff90ed2f36aac4e65ca9b0ea36389e7143b078f`,
Zephyr SDK 1.0.1 / GCC 14.3.0, CMake 3.28.4, West 1.4.0, and GitHub Actions
Ubuntu 24.04 runner image 20261026.354.1. Pinned West modules are cmsis
512cc7e895e8491696b61f7ba8066b4a182569b8, cmsis_6
30a859f44ef8ab4dc8f84b03ed586fd16ccf9d74, hal_nxp
2c2f28ac333e995d7279777409817c4b4f92c1ec, and littlefs
8f5ca347843363882619d8f96c00d8dbd88a8e79. The before/after workspace input
snapshots match byte-for-byte.

| Check | Current-revision result | Evidence and scope |
| --- | --- | --- |
| Host CI | PASS | [Run 41122359310](https://github.com/wonny-labs/crux-pilot/actions/runs/41122359310), revision `4100fd4`. Host debug and sanitizer CTest: 169/169 each; Python discovery: 358 tests; Logging consumer: 2/2; System consumer: 2/2. Format, dependency boundary, and static-analysis steps passed. |
| Syslog address validation | PASS, 2 test methods / 21 inputs | The Host run includes IPv4 and IPv6 syntax, family matching in both directions, brackets/scopes, TCP rejection, decimal port syntax, and 1..65535 bounds. |
| Zephyr composition matrix | PASS, 43 cases | Pinned Zephyr CMake composition builds on native_sim plus EVKB selection checks. Positive IPv4/IPv6 and dual profiles pass; family mismatch, malformed addresses, TCP, DHCP address selection, non-text output, missing dependencies, and invalid port cases reject. |
| native_sim Logging | PASS, 23/23 | UART-only 6/6, UDP-only 8/8, UART+UDP 9/9. The UDP profiles verify Syslog priorities `131/132/134/135`, selected-backend output, concurrent fan-out, collector absence, route-down, simulated L4 disconnect, and recovery. |
| EVKB Rev B full-image builds | PASS, 3/3 | UART-only, UDP-only, UART+UDP built from `tests/logging_consumer/zephyr` with common dependencies and toolchain. Resolved `.config`, `.dts`, map, `.stat`, ELF, BIN, ROM/RAM reports, compile commands, per-image provenance, and build logs are retained in Zephyr artifact `zephyr-41122362359-1` from [run 41122362359](https://github.com/wonny-labs/crux-pilot/actions/runs/41122362359), artifact ID 13010962608 (expires 2026-11-13). All 11 manifested artifacts per image match their SHA-256 records. |
| Zephyr CI | PASS | [Run 41122362359](https://github.com/wonny-labs/crux-pilot/actions/runs/41122362359) completed successfully on revision `4100fd4`; all workflow stages completed without failures. |
| Host static-analysis finding | Not reproduced | The earlier `system_consumer` pointer-size, similar-parameter, and missing-header diagnostic did not appear in the current Host analysis or the clean-main comparison (edea538f67abaa0acf7dba0fa8bc814941833a65, run 41117240528). The original diagnostic context is unavailable, so its cause remains unclassified. No diagnostic was suppressed. |
| Formatting and dependency policy | PASS | Zephyr patch style and Host format checks passed; the dependency check covers 84 portable sources. |
| Physical hardware | NOT USED | All validation is software-only. No image was flashed and no hardware was operated. |

The three build profiles resolved CONFIG_LOG_PROCESS_THREAD_STACK_SIZE to
2048 bytes and CONFIG_LOG_BUFFER_SIZE to 4096 bytes. UDP profiles resolved
CONFIG_LOG_BACKEND_NET_OUTPUT_TEXT=y and CONFIG_LOG_BACKEND_NET_MAX_BUF_SIZE
of 480 bytes. These are selected configuration values, not measured
runtime peaks.

| Profile | Zephyr ROM report root (whole-image Flash) | Zephyr RAM report root | .bin file size | Flash delta vs UART | RAM delta vs UART |
| --- | ---: | ---: | ---: | ---: | ---: |
| UART-only | 43,948 B | 13,932 B | 44,148 B | 0 B | 0 B |
| UDP-only | 116,220 B | 55,158 B | 116,420 B | +72,272 B | +41,226 B |
| UART + UDP | 116,632 B | 55,211 B | 116,836 B | +72,684 B | +41,279 B |

The comparison uses Zephyr's whole-image `rom_report` and `ram_report` roots. It
includes the application, kernel, networking stack, and all linked code and
data; the differences are not isolated UDP-backend overhead. The report and
provenance hashes, resolved configurations, Devicetree, maps, section reports,
ELF/BIN files, and build logs are available in the linked CI artifact.

Docker Desktop remains unavailable for local container builds. The user
authorized the branch push and remote GitHub Actions validation, so the current
Host and Zephyr CI runs supplied the Linux Host, composition, native_sim, and
EVKB evidence without installing tools or deleting local resources. The Docs
workflow is run separately against the documentation-only commit and its
result is recorded in the Gate 2 handoff. This report does not claim physical
network validation, production scheduling bounds, collector acknowledgement,
or runtime resource peaks.
