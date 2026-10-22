# M5 portable networking proposal

Status: architecture approved by the user on 22 October 2026 for software-only
implementation and qualification. Approval does not establish untested behaviour
or physical acceptance. Scope: [Issue #28](https://github.com/wonny-labs/crux-pilot/issues/28).

## Decision requested

Approve a deliberately qualified BSD/POSIX socket profile for IPv4 TCP and
connected UDP, plus a separate narrow Crux-owned read-only Network Interface
contract. Use the native Linux and Zephyr socket implementations. Do not add a
Crux socket wrapper or another network stack.

This approval would authorise software implementation and qualification of the
contract below, followed by independent source/evidence review and a Draft PR.
It would not qualify behaviour before tests, authorise physical operations,
merge, issue closure or M5 milestone completion. Material implementation findings
that invalidate this contract return to human review.

## Evidence and rationale

Starting point: `crux-pilot@94b11e43fcc745bf15abddcf48f43ab04384b29a`, fetched
from current main on 22 October 2026. Zephyr is pinned at
`684c9e8f32e4373a21098559f748f06915f950c9` (4.4.0).

Read the repository AGENTS.md, CONTRIBUTING.md, README, existing dependency
checks, controller native client and mock/real-stack tests, and accepted M3
validation record. Reviewed Issues #28, #24, #11 and the scope of concurrent
#22/#25. Planning references were read without modification from
`wonny-labs/crux-platform`: architecture concept blob
`5464a8b8b10e594d123e6cbab7712e820a026fe7`, architecture design blob
`b1e20234f2ef3d43689d33bdea6008f1935a39c2`, development plan blob
`5e2a4f4e783b7ec6d914132dd328d4bc49570845`.

The development plan explicitly requires comparing supported standards before
adding wrappers. M3 already uses nonblocking sockets, poll, SO_ERROR, bounded
monotonic deadlines and explicit cleanup. Its sample-owned native client is not
a portable public service. Its physical acceptance applies to identified native
profiles and explicitly does not qualify physical POSIX/M5 behaviour.

Reference semantics: [POSIX poll](https://pubs.opengroup.org/onlinepubs/9799919799/functions/poll.html),
[POSIX socket declarations](https://pubs.opengroup.org/onlinepubs/9799919799.2024edition/basedefs/sys_socket.h.html)
and [Zephyr socket documentation](https://docs.zephyrproject.org/latest/connectivity/networking/api/sockets.html).
Pinned source and actual qualification take precedence over assumptions based on
latest documentation or standard names.

| Alternative | Assessment |
| --- | --- |
| Qualified socket standard + owned interface status | Recommended. Fits the demonstrated client operations and runtime-shaped adapters while avoiding a second socket vocabulary. Requires precise qualification and native provider/composition ownership. |
| Owned socket + interface contracts | Could centralise deadlines and error translation, but currently duplicates standard operations and adds adapters. Reconsider if probes or consumer requirements demonstrate a material incompatibility. |
| Retain native sample access as the consumer API | Does not meet #28/#24. Retain historical fixtures and evidence; native facilities remain appropriate privately. |

An independent Challenger read the actual source, fixtures and pinned Zephyr
implementation. Verdict: PASS for recommending the first alternative; adoption
requires human approval. Key qualifications are incorporated below.

The concrete proposal received REVISE for ambiguous address eligibility and its
relationship to link state. After the clarification below, independent re-review
returned PASS on 22 October 2026, with no material design findings remaining.
This is a proposal review, not independent implementation acceptance.

## Consumers and responsibility split

The firmware example performs a bounded request/response exchange with a
composition-selected IPv4 peer over TCP and connected UDP. Application code
owns framing, payload verification, retry/restart policy and peer readiness.
An independently written synthetic runtime adapter exercises the same operations
and boundary. It demonstrates a representative porting seam, not compatibility
with an unspecified third-party runtime.

Crux providers/composition own Ethernet/native interface selection, native stack
startup, static addressing, board configuration and native resource observation.
The public interface status API reports observations. It does not promise that a
remote service is reachable. Link restoration does not reopen sockets or mark a
transaction successful.

## Proposed socket profile

- Public standard headers: `<sys/socket.h>`, `<netinet/in.h>`, `<arpa/inet.h>`,
  `<poll.h>`, `<fcntl.h>`, `<unistd.h>`, `<errno.h>` and the limited clock surface
  below, alongside ISO C. Consumers do not spell Zephyr/native names or includes.
- IPv4, `SOCK_STREAM`/TCP and `SOCK_DGRAM`/connected UDP; numeric addresses via
  `inet_pton`, `htons`/`ntohs` and required address types.
- `socket`, `fcntl(F_GETFL/F_SETFL)` with `O_NONBLOCK`, `connect`, `poll`,
  `getsockopt(SOL_SOCKET, SO_ERROR)`, `send`, `recv` and `close`. TCP server
  listen/accept and unconnected UDP are fixture/provider facilities, not public
  profile commitments in this slice.
- `-1` and symbolic `errno` on syscall errors; no portable numeric errno values
  or wholesale equivalence of native error sets. Qualify invalid descriptors,
  argument errors, would-block, interruptions, failed connection and exhaustion
  as applicable. Poll readiness is not a success result for connect or I/O.
- One owner per socket, thread context only. No concurrent close/use, descriptor
  sharing, cancellation or cross-thread shutdown guarantee. Callers own buffers;
  send completion means local acceptance, not remote delivery.
- All communication descriptors are nonblocking. Connect may return
  `EINPROGRESS`; poll completion is followed by SO_ERROR. Read SO_ERROR once for
  connection outcome: pinned Zephyr does not provide standard clear-on-read
  parity. Failed/timed-out connections are closed and replaced, not reused.
- Qualify `clock_gettime(CLOCK_MONOTONIC)` only for elapsed networking deadlines.
  Recompute remaining time after EINTR/EAGAIN, round positive sub-millisecond
  waits upward and clamp poll intervals to the integer range. An expired overall
  deadline terminates retries; timeout zero is a nonblocking check. Timing is
  subject to scheduler granularity, not a worst-case real-time guarantee. This
  introduces no new owned clock API and does not change #25 execution contracts.
- TCP returns short positive counts; callers advance offsets within one overall
  deadline. Receive zero means orderly EOF. Poll HUP/ERR must lead to consuming
  the actual operation error/EOF. Zero-length TCP calls are not an EOF probe.
- UDP sends one whole datagram or fails; successful zero-length datagrams are
  valid. Receive zero is a datagram, not EOF. This profile does not promise
  original-length reporting after truncation. Consumers provision a declared
  protocol maximum plus one guard byte and reject unexpected lengths; excess
  bytes are discarded with the datagram. General MSG_TRUNC/recvmsg support is
  deferred unless a concrete consumer requires it.
- Send flags are initially zero. Linux composition must suppress SIGPIPE through
  an explicitly documented process policy before use; it must not silently
  change a consuming process from a library constructor. Zephyr's signal/flag
  behaviour is not inferred from Linux. If the independent consumer requires a
  process-local alternative, return that material profile change for review.
- Close is attempted once; never blindly retry a numeric descriptor after an
  error, because it may already have been released and reused. Record cleanup
  uncertainty separately from descriptor ownership. Provider resource accounting
  remains private and is tested independently of syscall return values.

Proposed public usage target: `crux::sockets`. Host selects libc integration;
Zephyr selects the supported POSIX configuration. No published native include
paths, provider-private declarations or direct consumer linkage to backend
targets. Upstream implementation of standard headers may internally include
native headers; boundary checks distinguish that from consumer native API use.

## Proposed Network Interface contract

Public header `<crux/netif.h>` and usage target `crux::netif`; proposed function
`crux_netif_get_status(crux_netif_status_t *out)` uses existing `crux_status_t`.
Composition binds one logical primary interface. No native handles or host
interface names appear in portable consumer source.

On success return caller-owned values for administrative-up state, link state
(`UNKNOWN`, `DOWN`, `UP`), and availability of a selected configured IPv4 unicast
address, represented as four network-order octets. Absent address is explicit
and its bytes are zero. No address list, gateway, netmask or prefix is exposed
without a demonstrated consumer need. Multiple-address selection is a provider
policy recorded for each supported configuration, not arbitrary consumer
enumeration.

Eligible addresses exclude unspecified, multicast, limited/directed broadcast,
and tentative/duplicate addresses where the native provider reports those states.
Availability remains true while administrative or link state is down if an
eligible address remains configured; it implies neither routing nor remote
reachability. Host/native loopback fixtures explicitly select loopback addresses;
the RT1060 composition selects its static non-loopback address. IPv4 link-local
selection is not supported in this slice. Providers record any inability to
observe address eligibility states rather than claiming stronger validation.

Null output returns INVALID_ARGUMENT; missing/uninitialised selected interface
returns NOT_READY; observation failure returns IO. On error output is unchanged.
Callers never retain native pointers. Calls are allowed from threads, may be
made by independent callers and must not wait for link/address acquisition.
The returned value is a best-effort observation, not an atomic transaction with
native link changes or a guarantee about the next socket operation. Unknown link
must never be presented as up. Address availability is distinct from link state.

No public init/deinit, up/down, DHCP, static-address setter or subscription API
is introduced. Native addressing and startup remain explicit composition work.
Linux Host provides real interface observation where supported plus controlled
test seams; unsupported carrier observation returns UNKNOWN. Zephyr queries the
selected native interface privately. Neither provider derives application-level
service readiness from these fields.

## Integration, validation and limits

Use target-local standard allowances and negative controls for #24's boundary:
compile independent C/C++ consumers, reject direct native headers/names and
private targets, and inspect actual include/link propagation. Do not relax the
existing portable Time/base/controller checks globally or implement all of #24.

Select narrow Zephyr POSIX options rather than broad POSIX_API. Check the actual
dependencies of POSIX_NETWORKING, POSIX_DEVICE_IO, POSIX_FD_MGMT,
POSIX_SYSTEM_INTERFACES and clock support. Record added libc/descriptor/hostname
and socketpair configuration and resource consequences. Existing NET_TEST fixtures
disable compatibility aliases unless broad POSIX_API is used: use a separate
standard-header integration fixture rather than widening production promises to
make a historical test compile.

After approval, apply RED/GREEN/REFACTOR to behaviour. Use deterministic private
syscall/clock seams for short I/O, interruptions, deadline expiry, connect errors,
close uncertainty and interface transitions. Supplement with real Linux loopback
and Zephyr real-stack echo/silence/EOF/reconnect/resource-return cases. Treat
emulated carrier transitions as software evidence only. Reuse M3 oracle patterns
without rewriting accepted historical outcomes or adopting global context counts
as a multi-client ownership model. Build the selected RT1060 composition without
programming it. Run affected Host/sanitizer, native integration, warning/format,
analysis/dependency checks and applicable M3 regression. Independent review must
read actual source, public contracts, tests and retained logs before Draft PR.

Initial unchanged baseline: 42/42 Host tests, Host sample scenario and existing
dependency script passed. Compatibility spikes are throwaway investigation,
not supported-API tests.

### Investigation results before approval

On 22 October 2026, Linux amd64 container GCC 13.3.0 compiled the same standalone
C17 probe with standard headers for Host and Zephyr native_sim/native/64.
Both executions printed `SOCKET_STANDARD_HEADER_SPIKE_PASS`; native_sim exited
successfully at its explicit 1-second simulation stop. The probe checks socket
creation, numeric IPv4 conversion, nonblocking fcntl, zero-time poll, monotonic
clock_gettime, SO_ERROR and close. It does not connect, send or receive data,
exercise netif status, or qualify deadlines/failures/resources.

Subsequent implementation investigation showed that the early native_sim build
used CONFIG_NATIVE_LIBC: Zephyr's lib/CMakeLists.txt excludes its POSIX library in
that mode, so standard calls resolved to Host libc. The early probe therefore
provides no Zephyr socket-provider qualification. The full integration fixture
explicitly selects embedded Picolibc and checks actual native context allocation.

Native configuration has POSIX_SYSTEM_INTERFACES, POSIX_NETWORKING,
POSIX_DEVICE_IO, POSIX_FD_MGMT, POSIX_TIMERS, POSIX_MONOTONIC_CLOCK,
NET_DRIVERS/NET_LOOPBACK, IPv4/TCP/UDP and namespace compatibility enabled;
POSIX_API and NET_TEST remain disabled. Explicit ZEPHYR_MODULES selects only
this worktree, avoiding duplicate discovery of the manifest self module.

Local sources and full logs remain in ignored `build/investigation/`:

- Probe `socket-spike.c` SHA256:
  `3decc726bdae40c30eb303a009d535fade778bece08c7a708fb239c90530e7a4`.
- Probe `socket-spike/prj.conf` SHA256:
  `9c9b5cf97b02c6e199569424d86099d72c6094e652e8de414200ab1cc60f37d0`.
- Generated `spike-native-qualified/zephyr/.config` SHA256:
  `9dd989c54b5b4b9b1217a5795e68b345c74ca1b504ead18100ee70e06b1b01b8`.
- `baseline-host-retry.log`, `spike-host-run.log`,
  `spike-native-qualified-build.log` and `spike-native-qualified-run.log`
  retain passing outputs. Earlier logs retain failed container Git-path/ownership
  setup, duplicate module discovery and the superseded incomplete loopback
  configuration. Those failures were not product behaviour tests.

No supported capability implementation, full profile conformance, sanitizer or
EVKB build/physical result is claimed by this investigation. The baseline
dependency script does not establish #24's stronger consumer enforcement.

Excluded: IPv6, DNS, TLS, raw/multicast/broadcast socket guarantees, arbitrary
options/flags, runtime address changes, automatic reconnection, full runtime
compatibility, cross-OS real-time parity and concurrent-load qualification.
Future physical #28 acceptance requires a separately authorised identified
RT1060 controller path using the chosen portable contract, including physical
link loss/recovery and resource evidence. M3 logs cannot satisfy that new claim.
