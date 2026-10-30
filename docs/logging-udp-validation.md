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
so ERROR/WARN/INFO/DEBUG map to priorities 131/132/134/135. UDP has no peer
acknowledgement. `CRUX_OK` reports that Crux submitted a valid record to the
native logger; it does not promise queue admission, socket success, collector
receipt, or persistence. Zephyr's native deferred logger remains the only
worker and queue; Crux adds neither.

## Failure and lifecycle limits

Zephyr 4.4.0's native NET backend lazily creates and connects one socket when
the deferred logger first processes a record. Its UDP output uses
`zsock_sendmsg(..., ZSOCK_MSG_DONTWAIT)`; the native output callback returns the
formatted length even when the socket send fails. A send error therefore is not
visible through `crux_log_write()` and does not trigger socket recreation. With
the connection-manager option enabled, L4-connected activates the backend and
L4-disconnected deactivates it. The event handler does not close the socket;
reactivation reuses it.

The native_sim loopback fixture cannot produce the physical connection
manager's route lifecycle. The current test now takes the interface down,
verifies 16 direct UDP probe sends fail, and submits 16 uniquely tagged Crux
records while the native NET backend remains active and before simulated
L4-disconnect. It checks that no burst record reaches the collector and, in the
dual profile, that UART observes all 16 records. It then injects L4-disconnect,
checks that the backend deactivates, submits a separate inactive-backend burst,
restores the route, injects L4-connected, and observes a recovery record. The
direct probe establishes the simulator's route-down condition; Zephyr's NET
backend does not expose its individual send error to the test or
`crux_log_write()`. These checks exercise the active output path and event
handler under a simulator route change, not physical connection-manager events
or physical route recovery. The current test changes have not yet run in
native_sim because the local container engine is blocked, so this describes the
test design rather than a current-revision result.

In the earlier candidate, the no-collector case closed the test receiver while
keeping the local network route up. UDP does not tell the sender that no process
is listening, so that case checks best-effort submission and UART independence
only; it does not exercise a sender-visible error. That candidate's dual
native_sim run observed a maximum
`crux_log_write()` submission time of 0 ms at millisecond resolution and the
last UART record 20 ms after the final submission in the no-listener,
L4-down, and route-down bursts. These are observations from this simulator run,
not worker-latency bounds or WCET evidence. The UDP-only profile has no UART
observation. The native_sim fixture's 1536-byte receiver-thread stack belongs
to the test collector, not Crux or the NET backend. Remote delivery, production
scheduling bounds, physical network behavior, and target runtime peaks remain
unqualified.

The current tracked test and consumer configurations set Zephyr's deferred
logging thread stack to 2048 bytes and its log buffer to 4096 bytes. The current
UDP profiles set `CONFIG_LOG_BACKEND_NET_MAX_BUF_SIZE=480`, matching Zephyr's
recommended IPv4 UDP payload size. These are configured limits, not measured
runtime peaks. Earlier EVKB resolved configurations confirm those values for
candidate `44ac820…`; current-revision resolved configurations and whole-image
reports remain unavailable until the Dev Container build works.

## Current verification record — 2026-10-30

The current locally committed candidate is based on Crux
`ad876215f38859f03d70a3253d05db53255eafca` and has source/configuration/test
identity `f9ec7aa5c647c07cab65491ff291e585111d7bb3e3258c817c363654fc1f40f9`.
Its tracked patch and changed-file manifest are recorded in
`build/gate2/review-fixes/source-identity.txt`. The validation document itself
is excluded from that identity so the hash is not self-referential. The feature
branch has a local commit and remains unpushed.

| Check | Current-revision result | Evidence and scope |
| --- | --- | --- |
| Syslog address validator | PASS, 2 test methods / 21 inputs | `tests/build/test_syslog_server.py`; IPv4/IPv6 forms and family matching, syntax, bracket/scoped-address rules, TCP rejection, and port boundaries. |
| Crux CMake guard probe | PASS, 9/9 cases | Host CMake 3.28.4 configured the actual Zephyr port `CMakeLists.txt` with minimal stub targets. IPv4/IPv6 text cases passed; non-text, family mismatch, malformed IPv6, ports 0/65536, TCP, and DHCP-provided Syslog address settings rejected. This is a guard integration probe, not a Zephyr `west` build. The pre-fix equivalent accepted dictionary output; the current guard rejects it. |
| Declared Zephyr resource limits | Source configs set 2048-byte logger stack, 4096-byte log buffer, and 480-byte NET buffer; current resolution BLOCKED | Source files specify these values. The earlier EVKB resolved configs confirm them for identity `44ac…`; current `.config` resolution and whole-image resource reports remain outstanding. |
| Fresh independent review | Reviewer: REVISE; Challenger: REVISE | Both reviewed identity `f9ec7aa…` and the updated record. Reviewer closed the source-configured resource-limit documentation finding. Challenger considers compile-time Kconfig exclusion adequate when evidenced by the current resolved config. Both still require current Zephyr composition/native_sim, EVKB, Linux Host, and analysis evidence; current resolved configs are unavailable. |
| Dependency policy | PASS, 84 portable sources | `scripts/check-dependencies.sh`. |
| Formatting | PASS | `scripts/check-format.sh` with clang-format 18.1.8 from the task-local tools environment. |
| Python syntax and diff whitespace | PASS | `py_compile` for the validator/composition tests and `git diff --check`. |
| Zephyr composition matrix | BLOCKED | Docker Desktop container creation hangs before a container appears. The updated address/output composition cases have not run in the pinned Dev Container. |
| native_sim Twister | BLOCKED | The updated active-backend failure burst, dual-backend concurrent fan-out assertions, and 480-byte NET buffer configuration have not run. Do not treat the previous 23/23 run as current-revision evidence. |
| Linux Host suite and consumer CTests | NOT RERUN | The supported Host checks require the Linux Dev Container; no current-revision result is claimed. |
| macOS Logging consumer CTest | Policy PASS, 1/1; runtime build BLOCKED | The policy target built and passed. The actual Linux logging-port target fails to compile on this unsupported macOS host because AppleClang does not declare `sigtimedwait`; see `macos-host-integration-build.log` and `macos-host-policy-test.log`. |
| Host static analysis | NOT RERUN | The current Dev Container is unavailable. The `system_consumer` source and its Host target were not changed by this follow-up. See prior comparison below. |
| EVKB Rev B whole-image builds | NOT RERUN | All three current-revision profiles remain outstanding; previous size results are retained below as earlier-candidate evidence only. |
| Hosted CI | NOT RUN | The candidate is unpushed; remote CI was not used. |
| Physical hardware | NOT USED | All work remains software-only. |

Docker Desktop 4.95.0 and Engine 29.8.2 are installed. Read-only API calls and
inspection of the pinned image and cache volume work, and Docker reports zero
running containers. Container creation hangs with both the C++ image and a
minimal echo command; the Docker Desktop host log records a broken pipe while
forwarding its internal container-start stream. `docker desktop restart` failed
to stop the Desktop processes before its context deadline. A bounded
`docker desktop start` reported that Desktop was already running, while
`docker desktop status` still reports `stopping`. No container, image, volume,
cache or worktree was deleted. The Troubleshoot screen's Restart control was
disabled while the engine was stopping. A graceful app quit followed by
`docker desktop stop --timeout 30` also timed out with Docker helper processes
still running; a subsequent start again reported "already running", and the
app relaunch timed out. No process was force-killed.

A host-side fallback was inspected. The Zephyr source and modules are present,
and the cached West 1.4.0 environment uses Python 3.9.6, but Zephyr 4.4.0
Twister requires Python 3.10 or newer. CMake, Ninja, and the pinned ARM Zephyr
compiler are not available on the host, and no alternate container runtime or
Dev Container CLI is installed. Thus no local route using the pinned build
environment is available without installing a new toolchain. The current
container-start failure remains a Gate 2 blocker until local validation is
restored or separately authorized remote validation is chosen. The current
remote Host, Zephyr, and Docs workflows each declare `workflow_dispatch`; after
an authorized branch push, they can be manually dispatched on the candidate
branch without opening a PR. This route has not been used.

### Earlier candidate evidence — not current-revision verification

The previous full local run used candidate identity
`44ac820d99fc790ba444c17b33f9674db23be1acdd088aab0da0606ba57c0b30`, before
the Reviewer/Challenger follow-up changes. It produced the results below; they
are retained for traceability and must not be reported as passing on the current
identity.

| Check | Earlier candidate result | Evidence and scope |
| --- | --- | --- |
| Host suite | PASS, 169/169 | `scripts/check-host.sh`. |
| Logging consumer Host CTest | PASS, 2/2 | `scripts/test-logging.sh --host-only`. |
| System consumer Host CTest | PASS, 2/2 | `scripts/test-system.sh --host-only`. |
| Syslog address validator | PASS, 19/19 | Previous standalone input matrix. |
| Zephyr composition matrix | PASS, 37/37 | Before non-text and positive-IPv6 composition cases were added. |
| native_sim Twister | PASS, 3 profiles / 23 cases | Before the active-backend route-down burst and 480-byte test buffer. |
| Formatting and dependency policy | PASS | clang-format 18.1.8; 84 portable sources. |
| Host static analysis | PASS, exit 0 | The earlier `system_consumer` pointer-size, similar-parameter, and missing-header diagnostics did not reproduce. |
| EVKB Rev B cross-builds | PASS, 3/3 | Whole-image builds for UART-only, UDP-only, and UART+UDP. |

The clean-main Host suite itself did not finish: its CTest runner stopped
progressing after starting `Client.FailedCloseIsNeverRetried`. The baseline
Logging and System consumer CTests and full static analysis did pass. The
`system_consumer` analysis section was identical for the earlier candidate and
clean main with no emitted diagnostic. The original failing analysis context is
unavailable, so the old diagnostic remains not reproduced and its cause cannot
be classified more specifically than environment/context-dependent. The
current follow-up does not modify that consumer, but its analysis was not
rerun.

The earlier full local output is in `build/gate2/test-logging-ad87621.log`;
earlier Host/analyzer logs are in `build/gate2/ad87621/`, and the clean-main
comparison logs are in the sibling `issue-110-main-analysis` worktree. The
current validator, formatting, dependency, and CMake probe outputs are retained
under `build/gate2/review-fixes/`.

## Earlier EVKB Rev B whole-image resource record (not current revision)

All profiles use the same representative application, Zephyr west dependencies,
Zephyr SDK 1.0.1, GCC 14.3.0, board `mimxrt1060_evk@B//qspi`, and Zephyr commit
above. The UDP profiles use IPv4 Syslog `192.0.2.1:15141` and a 480-byte native
NET backend buffer. The resolved configuration files confirm the selected
backends and networking options. The map and size reports show **whole-image**
measurements, including the application, Zephyr kernel, network stack and all
linked code/data; their differences must not be interpreted as pure backend
overhead.

| Profile | Linker FLASH | Linker RAM | DTCM | RAM + DTCM | ROM report root | RAM report root |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| UART-only | 44,148 B | 13,952 B | 0 B | 13,952 B | 43,948 B | 13,932 B |
| UDP-only | 116,420 B | 41,272 B | 13,936 B | 55,208 B | 116,220 B | 55,158 B |
| UART + UDP | 116,836 B | 41,336 B | 13,936 B | 55,272 B | 116,632 B | 55,211 B |

Relative to UART-only, the UDP-only whole image is +72,272 B FLASH, +27,320 B
linker RAM, and +13,936 B DTCM. The dual image is +72,688 B FLASH, +27,384 B
linker RAM, and +13,936 B DTCM. The dual image is +416 B FLASH and +64 B
linker RAM relative to UDP-only. These deltas include all profile-dependent
linking and are not an isolated measurement of the logging backend.

Resolved `.config`, Devicetree, linker map, ELF, BIN, ROM/RAM reports, section
sizes, per-image hashes, module revisions, and toolchain provenance are
preserved locally under `build/gate2/evkb-ad87621/`. The reported image sizes
are build-time link results, not runtime peak memory or physical Ethernet
qualification.
