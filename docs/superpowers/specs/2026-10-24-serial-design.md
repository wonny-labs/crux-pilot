# Portable Serial — proposed public contract and design

**Status: approved for software implementation by the user on 24 October 2026**
through the explicit reply “설계·계획 승인 — 제안대로 구현”, covering design/plan
commit `d9e39ab`. Issue #27; baseline `98f05104ad2bc915e4da93ab9e8732e850c140c5`.
The [M5 Pilot plan](../../m5-pilot-validation-plan.md), existing component contracts,
AGENTS.md and CONTRIBUTING.md govern scope. No hardware, merge or closure authority.

## Need, alternatives and recommendation

Provide a bounded, binary UART byte stream to a portable reference-controller
scenario and a separate runtime-shaped C17/C++17 consumer. Existing M3 UART use
is diagnostic/Shell/logging output, not an existing portable data-port contract.
No product protocol or detailed runtime Serial requirement has been supplied;
the proposed representative workload is a small request/response with short-I/O
handling and an application-owned transaction deadline. It is a proposed test
consumer, not evidence of a completed third-party port.

| Approach | Assessment |
| --- | --- |
| Qualified POSIX `open/read/write`, `termios`, `poll` | Good Linux native implementation. The pinned Zephyr composition has no qualified matching UART termios/device-file interface. Adopting it publicly would require an additional TTY/VFS integration and new standard exceptions in the consumer policy. Do not equate native `tty_read/write` with POSIX support. |
| Four-function Crux Serial contract | **Selected with human approval.** Express logical binding, partial progress, finite waits and exclusive lifetime without public native types; use native TTY/UART facilities privately. |

The pinned Zephyr `subsys/console/tty.c` was also examined. It resets relative
waits during multi-chunk operations and writes a `~` marker when RX overflows;
that is unsuitable unchanged for a binary stream with one functional deadline.
Reuse native drivers, ring buffers and kernel waits, not those console policies.
Do not add a generic OSAL, device registry, protocol/framing or logging framework.

## Proposed public surface

Owned header: `components/serial/include/crux/serial.h`; target: `crux::serial`.
C17 with C++17-compatible declarations; only standard integer/size types and
`<crux/status.h>` in the public header. Names below are proposals, not existing APIs.

```c
typedef uint32_t crux_serial_port_t; /* Project-defined logical ID; zero is valid. */
typedef struct { uint32_t slot; uint32_t generation; } crux_serial_t;
typedef enum {
    CRUX_SERIAL_PARITY_NONE,
    CRUX_SERIAL_PARITY_EVEN,
    CRUX_SERIAL_PARITY_ODD
} crux_serial_parity_t;
typedef struct {
    uint32_t baud_rate;
    uint8_t data_bits;
    uint8_t stop_bits;
    crux_serial_parity_t parity;
} crux_serial_config_t;

crux_status_t crux_serial_open(crux_serial_port_t port,
                               const crux_serial_config_t *config,
                               crux_serial_t *out);
crux_status_t crux_serial_read(crux_serial_t serial, void *buffer, size_t capacity,
                               uint32_t timeout_ms, size_t *transferred);
crux_status_t crux_serial_write(crux_serial_t serial, const void *buffer, size_t size,
                                uint32_t timeout_ms, size_t *transferred);
crux_status_t crux_serial_close(crux_serial_t *serial);
```

### Configuration and binding

- Configure on open; no live reconfiguration. Proposed supported requests:
  baud 9600/19200/38400/57600/115200; 7/8 data bits; 1/2 stop bits; none/even/odd
  parity. A provider must reject an unavailable combination with NOT_SUPPORTED,
  never silently substitute. Baseline qualification: **115200, 8N1, no flow control**.
  Other settings receive mapping/rejection tests; physical qualification remains
  setting-specific. Zero baud, invalid parity enum, data bits other than 7/8 or
  stop bits other than 1/2 are INVALID_ARGUMENT. Other nonzero baud rates outside
  the listed set are NOT_SUPPORTED.
  Settings inside that set may succeed before physical qualification; this does
  not qualify their electrical behaviour. Unsupported native combinations still
  fail explicitly rather than silently falling back.
- Raw binary bytes: no line editing, echo, newline translation or software flow
  control. Hardware flow control, RS-485/half-duplex, break/modem-control and 9-bit
  operation are unsupported in this slice. No USB CDC, SPI/I2C/CAN or DMA requirement.
- Project/native composition binds immutable logical IDs to UART devices or owned
  Linux TTY paths before first use. No public device pointers, paths or registration
  API; no automatic probing/opening of a host device. Missing binding is
  INVALID_ARGUMENT; configured device absent/unready is NOT_READY.

### Lifetime, ownership and concurrency

- Task/thread context only; callbacks, public asynchronous buffer borrowing and ISR
  calls are outside the contract. UART is an asynchronous byte stream electrically;
  the application API is synchronous with bounded waiting.
- One open owner per bound physical device. Duplicate open is BUSY. Aliased IDs
  must not bypass this rule. Handle representation is reserved; zero is invalid,
  retired copies are INVALID_STATE, generation exhaustion retires a slot safely.
- Open requires a non-live output variable and zeroes it on failure. Failed native
  configuration is not claimed to roll back; keep a device unavailable if cleanup
  cannot establish safe ownership/state.
- One read and one write may overlap (full duplex). A second operation in the same
  direction returns BUSY; it does not queue behind the active caller. Metadata
  admission contention may also return BUSY. No application-wide serialization or
  dependency on `crux::thread`/`crux::sync` is introduced.
- Close while a call is admitted returns BUSY and preserves the handle/resources.
  Caller stops new calls and serializes writes to the handle variable. Close does
  not cancel somebody else's waiting call.
- **Close is abortive, not a drain or delivery acknowledgement.** It discards
  provider-buffered RX/TX and detaches callbacks; bytes already in hardware/on the
  wire cannot be recalled. Successful cleanup clears the handle. Native cleanup
  error returns IO and also consumes the handle; do not retry a potentially reused
  descriptor. Quarantine an uncertain binding until composition can re-establish
  its safe state. For a valid handle, BUSY preserves the live resource; argument
  and stale-handle validation failures perform no teardown.

### Transfers, deadlines and errors

- Buffers are borrowed only until return. Write copies accepted bytes into
  provider/native storage; caller may then reuse its buffer. Read writes only the
  returned prefix. Mandatory `transferred` is set to zero before validation when
  its pointer is valid. Caller supplies non-overlapping valid buffer/count storage.
- For nonzero requests, OK means `1 <= transferred <= requested`; short progress
  is normal, not TIMEOUT. Return after progress rather than filling an entire
  request. Errors report zero transferred and preserve the receive buffer. No
  record boundaries, exact-length exchange, retransmission or rollback is provided.
- Check known faults before commitment. Once a positive prefix is accepted/copied,
  return OK with that count even if a fault is subsequently observed; latch the
  later fault for the next call. Never report zero for an already committed prefix
  or invite its retransmission by returning TIMEOUT/IO after that commitment.
- A successful write acknowledges **acceptance into a bounded transmit queue**,
  not wire completion or peer receipt. Application framing/acknowledgement owns
  delivery. Pending data may be lost on abortive close or a later device fault.
  A public drain/flush API is deferred unless an actual consumer requires it.
- Zero-length I/O is a validated OK/zero no-op; null data is allowed only then.
  Null output count/config/handle pointers are INVALID_ARGUMENT. Unknown handle is
  INVALID_STATE; null close pointer is INVALID_ARGUMENT.
- `timeout_ms == 0` is a nonblocking poll: no progress or metadata contention ->
  BUSY. `1..INT32_MAX` is one monotonic functional budget from entry, including
  admission/waits; larger values -> OUT_OF_RANGE. No forever sentinel. Spurious
  wake-ups and EINTR never restart the budget. A wait which returned TIMEOUT
  cannot later commit bytes during cleanup. Check expiry before a new commit.
- Finite expiry without progress -> TIMEOUT. No-data on a UART is not EOF. Host
  `read == 0` alone does not prove disconnection; inspect its native poll/error
  conditions. An observed device/hangup/transfer error -> IO. A silent or physically
  unplugged UART peer can merely time out: no generic cable-presence detection.
- Report observed RX overflow or native framing/parity/overrun errors as IO, not
  successful binary data. A detected stream-loss/device fault latches a failed
  instance until close/reopen; no silent recovery over an unknown gap. Native
  ports must document what their driver can actually detect. This is not a
  checksum, protection against all unreported wire corruption or Linux PTY proof
  of electrical UART errors.
- NOT_SUPPORTED covers unavailable driver/configuration features; NO_RESOURCES
  covers slot/native-resource exhaustion; IO covers remaining native failures.
  Do not invent new public result codes. Before commitment, check arguments,
  lifetime/admission, known device faults and expiry, then attempt progress.
  Positive commitment is authoritative; a later fault affects subsequent calls.
- Functional wait bounds are not strict whole-call/WCET guarantees: scheduling and
  safe final bookkeeping may delay return. Open/configuration and close/native
  teardown have no public timed-operation guarantee. Keep native waits outside
  metadata/spin locks and disclose any native lifecycle latency limitation.

## Native implementation and ownership evidence

**Linux Host:** private POSIX TTY port with nonblocking descriptor, explicit raw
termios settings and `poll`/monotonic remaining budget. Use PTYs for actual host
I/O tests plus controlled native-result seams for deterministic faults. Private
trusted composition binding follows existing File Host bind-before-use practice.
Request TTY exclusivity before configuration, reject duplicate device identities
and preserve/restore native settings where safe. Kernel exclusivity does not
revoke already-open descriptors or control arbitrary external programs; the host
composition must own its dedicated test TTY. Never claim universal OS-wide locking.

**Zephyr:** private interrupt-driven UART, native ring buffers, kernel wait/signals
and short ISR-safe protection. No UART poll-out in the finite-wait path and no
FIFO calls from thread context where the native API requires ISR context. No DMA
or new worker thread. Static per-port storage/callback context survives retirement;
  native callback remains a per-device service routine, not a session/operation
  closure. Serialize its native accesses with the port's retirement gate; it
  returns harmlessly during closed/configuring state and references no retired
  session or caller buffer. Native drivers can invoke a previously cached callback
  after detach, so detach is not proof of quiescence. A late invocation after reopen
  may only service the current device state through the permanent context and gate.
  Check native errors even when RX/TX-pending reports no work. Native ISR work and
  copied chunks remain bounded for the selected driver/profile. Do not reuse
  console TTY overflow/output policy.

Initial Zephyr test composition: **one logical port, 256-byte RX and 256-byte TX
rings**, plus explicitly measured metadata/kernel resources. Host staging/copy
chunks are bounded to 256 bytes; kernel TTY queues have separate native bounds,
not a claimed 256-byte total capacity. These are configurable Pilot resource
limits, not permanent platform maxima or guaranteed sustained throughput.
Close/reopen does not create a wire-level message epoch: bytes already in native
hardware or in transit can remain. The consumer's transaction identity/framing
must reject an old response rather than relying on close as a protocol reset.

The candidate EVKB mapping is logical port 0 -> **LPUART3**, using the existing
`arduino_serial` pin group AD_B1_06 TX / AD_B1_07 RX; enable it only in the Serial
test/application composition. Keep LPUART1 console/Shell/logging unchanged.
The board also aliases LPUART3 as `m2_hci_bt_uart`; it is not automatically a free
port. The pinctrl records LPUART3/LPUART8 conflicts with CSI. Reject Bluetooth HCI
UART use or a composition enabling overlapping resources. Connector/electrical access is not
yet physically verified and is an entry gate for any physical proposal.

Reject duplicate UART bindings and active Console/Shell/UART Logging/mcumgr or
other known native UART owners before installing the callback. Include negative
composition tests. `device_is_ready()` and callback installation are not ownership
proofs; unregistered direct native users violate the composition contract. Do not
rewrite M3 diagnostics or make the future Logging component depend on Serial.

## Verification, delivery and physical boundary

Use existing GoogleTest/CTest, Twister/ztest and native-free CMake/source guards.
Host proves portable lifecycle/deadline/error cases; Linux PTY tests prove actual
TTY short/binary I/O and observed hangup; Zephyr `zephyr,uart-emul` proves the real
IRQ port, not electrical timing. Do not copy the whole Host suite into ztest.
An independent C17/C++17 consumer and reference-controller-shaped transaction use
only `crux::serial` and its public header; native setup/peer code is separate.

Run affected checks in development, final full applicable Host/Zephyr CI once the
final source is fixed, independent Reviewer and Challenger, one EVKB cross-build
of the selected composition, then a Draft PR. Map all Issue #27 criteria to actual
evidence and explicitly leave physical support pending. No new test framework or
general capture/provenance system is required.

Minimum later physical proposal: exact reviewed image and isolated UART ownership,
verified connector/voltage/wiring and peer identity, small binary request/response
with partial-transfer handling, one silent-peer timeout, cleanup/reopen, and
recorded configuration/resource/elapsed observations under predeclared limits.
Use existing safe capture mechanisms as applicable. No loopback/wiring, probe,
programming, reset, UART access or other hardware operation is authorised now.
No Production stress, indefinite soak, exhaustive electrical matrix or WCET claim.

## Source basis and approval requested

- Repo: Issue #27, `docs/architecture.md`, `docs/native-interface-policy.md`,
  `docs/thread-sync.md`, M3 Shell/deferred-output configs and tests/README.md.
- Pinned Zephyr `684c9e8f32e4373a21098559f748f06915f950c9`:
  `drivers/serial/uart_mcux_lpuart.c`, `uart_emul.c`, `subsys/console/tty.c`,
  EVKB DTS/pinctrl; pinned MIMXRT1062 feature header supports 7-bit/stop-bit config.
- [Zephyr UART model](https://docs.zephyrproject.org/latest/hardware/peripherals/uart.html)
  and [IRQ calling rules](https://docs.zephyrproject.org/latest/doxygen/html/group__uart__interrupt.html)
  supplement, not replace, pinned-source inspection.
- [Linux termios documentation](https://man7.org/linux/man-pages/man3/termios.3type.html)
  explains noncanonical/timeout differences. No full POSIX Serial conformance claim.

The human approval covers the Crux-owned four-function surface, buffered
short-transfer/abortive-close semantics, finite-timeout rules, selected
configuration subset and dedicated-UART binding/initial limits. Material contract
changes require a new decision; implementation approval does not authorise
hardware, merging or Issue closure.

Planning builder check found three material ambiguities: positive progress versus
a later fault, cached callback invocation after retirement, and permitted settings
versus physical qualification. This draft resolves them above; the independent
builder's affected re-read found no important planning question remaining. This
is buildability review only, not implementation/test PASS or hardware qualification.
