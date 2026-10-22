# Qualified portable networking slice

Status: selected architecture approved by the user on 22 October 2026 for
[Issue #28](https://github.com/wonny-labs/crux-pilot/issues/28). Support claims
are limited to the configurations and results in [software evidence](evidence/m5-networking-software.md).
Physical acceptance remains pending. The approved [design proposal](plans/m5-networking-proposal.md)
records alternatives and rationale; this document describes consumer use.

## Boundary and composition

`crux::sockets` publishes a qualified standard-interface profile, without wrapper
functions or another stack. `crux::netif` supplies `<crux/netif.h>` and existing
base statuses. Use these targets instead of implementation targets or private
include directories. Host builds select libc; Crux Zephyr composition selects
native providers and the required POSIX options. Firmware and runtime consumer
source uses the same standard headers/public contracts on both environments:
no Zephyr/NXP names, platform conditionals or provider calls.

The reference firmware consumer is `samples/networking/client.c`. A separately
written runtime-shaped adapter lives in `tests/networking/runtime_port.c`.
Neither establishes complete compatibility with a third-party runtime. Test peer
servers are fixture code outside the supported client API. Native stack startup,
addressing, threads, driver correction and resource inspection are controlled
composition/test responsibilities. Historical M3 source and physical evidence
remain unchanged and do not qualify this new portable path.

## Socket profile

| Area | Supported contract |
| --- | --- |
| Protocols | IPv4 TCP client and connected unicast UDP; one owner per descriptor |
| Headers | `sys/socket.h`, `netinet/in.h`, `arpa/inet.h`, `poll.h`, `fcntl.h`, `unistd.h`, `errno.h`, `time.h`, and ISO C |
| Address | Numeric IPv4 `inet_pton`, `htons`/`ntohs`, `sockaddr_in` and required standard types |
| Lifecycle | `socket`, `fcntl(F_GETFL/F_SETFL, O_NONBLOCK)`, `connect`, `close` |
| Communication | `send` and `recv`, flags zero, nonblocking descriptors |
| Readiness | `poll` with finite nonnegative millisecond timeout; IN/OUT/ERR/HUP/NVAL |
| Connect outcome | EINPROGRESS then writable/error readiness followed by `getsockopt(SOL_SOCKET, SO_ERROR)`; read outcome once |
| Elapsed time | `clock_gettime(CLOCK_MONOTONIC)` solely for networking deadlines |

Native and Host errors are `-1` plus symbolic `errno`; numeric values differ.
A poll result means an operation can be attempted, not that it succeeded. Poll
HUP/ERR must lead to the actual operation or SO_ERROR. Operations can still
return EAGAIN/EWOULDBLOCK after readiness; retry EINTR and would-block only while
one overall deadline remains. SO_ERROR clear-on-read parity is **not** promised:
pinned Zephyr returns its saved connection error without clearing it.

Only thread context is supported. No concurrent descriptor close/use, sharing,
thread cancellation or cross-thread shutdown contract is qualified. Callers own
buffers for each synchronous operation. A successful send reports local
acceptance, not remote receipt. Resource exhaustion is a recoverable error;
capacity and reclamation are provider/configuration dependent.

TCP permits short positive send/receive results. Application framing advances
by that count and shares the existing deadline; receive zero is orderly EOF.
The example requires exactly 32 echoed bytes and treats early EOF as
ECONNRESET for that protocol. A zero-length TCP request is not an EOF probe.

Connected UDP sends one whole datagram or fails. Zero-length datagrams are valid,
and a receive result of zero is not TCP EOF. Receives consume one datagram;
excess bytes are discarded. This slice does not promise original-length
reporting for truncated input. Applications must declare a maximum protocol
payload and use a guard byte or another approved protocol bound. The example's
33-byte receive rejects every length other than its required 32 bytes; oversized
and zero-length input are protocol errors, not malformed socket semantics.

Close is attempted once, including after timeout, EOF and connect failure.
An error does not establish that the numeric descriptor remains owned; it may
already have been released/reused. Never blindly retry it. The example reports
`cleanup_uncertain=1` on close failure. Native pool/context observation is
separate qualification evidence, not a public ownership API. Socket reuse or
automatic reconnection after failure is not promised: close and create a fresh
socket according to application policy.

### Deadlines and Linux SIGPIPE

The example accepts a finite positive operation budget and uses a single
monotonic deadline across connect/send/receive. Positive sub-millisecond poll
remainders round upward; poll intervals clamp at INT_MAX. Interruptions and
would-block cannot renew the budget. Timeout zero in the standard poll profile
is a nonblocking observation; the sample exchange rejects zero/negative budgets.
No general clock framework, scheduling guarantee, interrupt-latency bound or
portable worst-case close/allocation time is introduced.

Linux executable composition must install its documented SIGPIPE policy before
using this profile. The provided Host executable and Host test composition
explicitly install `sigaction(SIGPIPE, SIG_IGN)`; reusable socket/netif code does
not change signal disposition. This affects the whole process and is an explicit
integration requirement for an independent consumer. Sending after peer closure
must return an error without killing that process. Zephyr uses its native socket
error behaviour. Flags such as MSG_NOSIGNAL are not silently added to common
consumer code. Consumers unable to accept this policy require a new contract
review rather than a hidden platform branch.

## Primary Network Interface observation

`crux_netif_get_status(crux_netif_status_t *out)` observes one composition-selected
primary interface. Null returns INVALID_ARGUMENT. Missing/uninitialised interface
returns NOT_READY, observation failure IO; all errors leave output unchanged.
Successful output is caller-owned and contains no native pointers:

- `administrative_up`: configured administrative state.
- `link`: UNKNOWN, DOWN or UP; UNKNOWN never means up.
- `ipv4_available`: a selected eligible configured address exists.
- `ipv4_address[4]`: network-order octets, zero when unavailable.

Address eligibility excludes unspecified/0-prefix, multicast/reserved high
addresses, limited/directed broadcast, IPv4 link-local, and tentative/duplicate
states where the provider reports them. /31 point-to-point and /32 endpoints
are not incorrectly classified as directed broadcasts. Selection is the lowest
eligible address in network byte order when several exist; this is an observation
policy, not multiple-interface management or source-address selection.

Availability is independent of admin/carrier state and remains true while down
if an eligible address remains configured. The snapshot implies neither routing,
socket binding nor peer reachability. Calls do not wait for link/address
acquisition; snapshots are best-effort and can be stale immediately. Independent
thread callers are supported, but no atomic transaction with native state changes
or real-time duration is guaranteed.

Linux defaults to composition setting `CRUX_NETIF_HOST_INTERFACE=lo` (CMake cache).
It uses a per-call getifaddrs snapshot, with native allocation released on return.
Loopback carrier follows its administrative state; other interface carrier is
UNKNOWN. Standard getifaddrs does not expose per-address duplicate/tentative
flags, so that exclusion is not asserted on Host. No Linux routing/netlink
manager or sysfs dependency is introduced.

Zephyr observes `net_if_get_default()` privately under its interface lock,
including native administrative/carrier flags and preferred IPv4 unicast entries.
That default-interface selection is composition policy. No public init/deinit,
up/down, address setter, DHCP control, gateway/mask query or subscriptions exist.

## Configurations, assumptions and unsupported scope

Host/native real-stack tests bind peers explicitly to 127.0.0.1; primary status
also refers to loopback. Standard routing delivers that traffic locally. The
configured primary interface does not bind those sockets. The RT1060 build
uses the isolated M3 topology: static board 192.168.77.2/24, peer 192.168.77.1,
ports TCP 42420/UDP 42421, no gateway/DHCP/DNS required. Its overlay reserves
GPIO1.9 for PHY reset and uses the recorded local test MAC. The composition
reuses the existing generated MDIO completion correction without altering
upstream sources. Compilation does not establish physical traffic or safety to
operate the board. A new physical plan/approval is required before target use.

The Zephyr fixture must use embedded libc; native_sim's external Linux libc can
silently bypass Zephyr POSIX implementations. Its test-only Picolibc module is
pinned to the same Zephyr manifest revision, with source/provenance checks.
Required POSIX options enable additional native facilities (e.g. hostname,
socketpair, full libc, descriptor/poll/select tables); their existence does not
qualify those APIs for consumers. `CONFIG_CRUX_SOCKETS` requires the options rather
than recursively selecting a general POSIX profile. Broad `POSIX_API` and NET_TEST
are not required. Pinned Zephyr's unistd.h hides the existing close declaration
behind broad POSIX_API. The Zephyr usage target automatically injects a private,
declaration-only header for standard `close(int)`, with C++ linkage protection.
It publishes no native include directory and implements no wrapper. Remove this
workaround once the supported upstream header exposes close for POSIX_DEVICE_IO;
the native link map must still resolve it to Zephyr device_io.c, not a libc stub.
Profile tests check actual Zephyr context allocation and return.

Native qualification uses six contexts/connections, 16 RX/TX packets and 64 RX/TX
buffers, with the configured 1500 ms TCP TIME_WAIT. Each sequential scenario
waits for zero contexts and fully restored pools (up to 500 ten-millisecond test
waits); the observed maximum settling time was 1520 ms. An initial unpaced run
exhausted contexts. Immediate reclamation, burst reconnect capacity and sustained
throughput are therefore unqualified. Holding six UDP sockets makes the next
native allocation fail with ENOENT; closing them permits fresh allocation. This
is a pinned-provider result, not a portable exhaustion errno guarantee. The
RT1060 build uses four contexts/connections, 16 RX/TX packets and 32 RX/TX buffers;
its runtime capacity and cleanup are not established by compilation.

Excluded: TCP servers, unconnected UDP, multicast/broadcast, DNS, IPv6, TLS,
raw sockets, arbitrary flags/options, multi-interface management, runtime address
changes, automatic recovery, comprehensive runtime compatibility, production
security and concurrent-load/long-run performance qualification. Physical link
loss, routing, MAC/PHY behaviour and target resource/timing results remain pending
unless identified physical evidence is separately accepted.
