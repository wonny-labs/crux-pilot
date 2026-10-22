# M5 networking software evidence

Status: software qualification and independent review PASS for the approved
slice. Physical acceptance is pending. The user approved this client-only
architecture on 22 October 2026, including explicit Host SIGPIPE composition and
narrow dependency enforcement. This does not authorise hardware operations,
merge, issue closure or M5 milestone completion.

The [consumer contract](../networking-contract.md) defines supported semantics,
ownership, deadlines, errors, interface/routing assumptions and exclusions. The
[approved proposal](../plans/m5-networking-proposal.md) records alternatives: a
qualified standard profile meets the representative consumers without a Crux
socket wrapper or another stack; interface observation needs its own small,
read-only public contract. Runtime address management remains deferred.

## Source and reproducibility

Qualified implementation/test source: `3cbf9a6473be8fd94e26e7321fb4c7a136a731d4`
on `feat/m5-portable-networking`, based on main `94b11e4`. Before delivery,
main advanced to `32405ad` with an independent isolated Flash PoC. A merge-tree
check succeeds without conflicts; its additional CI step is separate from the
networking changes. Local claims below remain tied to the recorded source,
not an untested merged revision. Subsequent evidence
edits do not change executable inputs. The adjacent
[machine-readable manifest](m5-networking-manifest.json) records SHA-256 hashes
of tracked source at that revision, retained raw logs and generated artifacts.
Final raw logs and review-regression RED/GREEN logs are also checked in under
[networking/](networking/) (same names, with `native-scenario.log` for the native
scenario output). Full raw evidence is retained locally in `build/investigation` and the portable
`build/evidence/m5-networking-software.tar.gz` bundle; hashes identify the exact
files, rather than an assumed successful command. Generated binary build outputs and the full exploratory log history are not
checked into Git; the curated raw review logs are. CI publishes networking configuration, map,
image and scenario outputs with the existing Zephyr artifacts; CI results are
separate from the local results below.

Zephyr is unchanged at `684c9e8f32e4373a21098559f748f06915f950c9` (4.4.0).
Native Picolibc is `01254932e8e81085817ed61fd858648584ffe37c`, from that same
Zephyr manifest, fetched into an external cache. The Linux amd64 development
image uses CMake 3.28.4, Ninja 1.11.1, Clang 18.1.3 for Host, GCC 13.3.0 for
native_sim, and SDK 1.0.1/GCC 14.3.0 for RT1060. The container mounts the parent
workspace read-only and only this worktree writable, without device access.
No upstream checkout, board/Flash layout or other task branch was modified.

Run in the existing Linux development environment, with `CRUX_WORKSPACE`,
`ZEPHYR_BASE`, `ZEPHYR_SDK_INSTALL_DIR` and `CRUX_CACHE_DIR` configured:

```sh
scripts/check-host.sh
CRUX_HOST_PRESET=host-sanitize scripts/check-host.sh
python3 -m unittest discover -s tests/build -v
CLANG_FORMAT=clang-format scripts/check-format.sh
scripts/analyze-host.sh
scripts/check-dependencies.sh
scripts/bootstrap-networking-tests.sh
scripts/test-networking.sh
```

Local debug and sanitizer runs used separate dependency caches
`/opt/crux-cache/networking-host-debug` and
`/opt/crux-cache/networking-host-sanitize` to avoid sharing generated GoogleTest
objects with concurrent work. Networking tests used `/opt/crux-cache` for SDK
and pinned Picolibc. `scripts/test-networking.sh` runs independent Host consumers,
builds/runs embedded-libc native networking, compiles the native C++ probe and
cross-builds the RT1060 sample. It never flashes, resets or opens a target.

The applicable regression invocation was:

```sh
west twister -j 2 -p native_sim/native/64 \
  -T "$repo/tests/controller_network" \
  -T "$repo/tests/controller_network_stack" \
  -T "$repo/tests/controller_integrated" \
  -T "$repo/tests/zephyr_time" -T "$repo/tests/controller_m3" \
  -O "$repo/build/zephyr/networking-regression" \
  --extra-args="ZEPHYR_MODULES=$repo" --inline-logs
```

Here `repo` is this worktree's absolute path and the command runs from the parent
west workspace, as recorded in the raw log. These are software regressions, not
a repetition or extension of accepted physical M3 results.

## Completed checks and direct evidence

Paths below are relative to the worktree/bundle root.

| Check | Observed result | Raw evidence |
| --- | --- | --- |
| Host debug | 71/71 tests, existing stdin/stdout sample scenario passes | `build/investigation/host-debug-verified-retry.log` |
| Host ASan/UBSan | 71/71 tests, existing sample scenario passes | `build/investigation/host-sanitize-verified-retry.log` |
| Build/dependency tooling | 161/161 Python tests | `build/investigation/build-tests-delivery.log` |
| Dependency boundary | Unchanged existing checks plus narrow networking checker pass | `build/investigation/dependencies-delivery.log` |
| Formatting | Exit 0, no differences | `build/investigation/format-delivery.log` |
| Static analysis | Final full command exit 0; system-header warnings suppressed, no owned diagnostics | `build/investigation/analysis-delivery.log` |
| Independent public consumers | C and C++ configure/build/run; only owned netif include and `crux::base` transitive requirements | `build/investigation/networking-command-final.log`, `build/networking-consumer/public-requirements.txt` |
| Native standard C++ declarations | Probe compiles using actual native client command and C++ linkage | `build/investigation/networking-command-final.log`, native `cpp-probe.o` |
| Actual Zephyr stack | 3/3 ztests pass; embedded libc, native context allocations observed | `build/zephyr/networking-native-pico/scenario.log`, `.config`, `.map` |
| Applicable regressions | 8/8 executed configurations; 43/43 cases, zero failures/errors/warnings | `build/investigation/zephyr-regression.log`, `build/zephyr/networking-regression/twister.json` |
| RT1060 | Cross-build/link passes: FLASH 91656 B, RAM 31708 B, DTCM 13936 B | `build/investigation/networking-command-final.log`, `build/zephyr/networking-evkb/zephyr/{.config,zephyr.map,zephyr.elf,zephyr.bin}` |

Deterministic tests substitute syscalls/clock only in private test object targets;
production objects are compiled separately. RED/GREEN logs cover netif output
preservation/eligibility and Host provider selection, short transfers, EINTR,
EAGAIN, finite deadline expiry (including readiness delivered after expiry),
connect/SO_ERROR failures, TCP EOF, UDP protocol lengths, clock failure and one
close attempt with uncertain cleanup. New regressions reproduced the review
findings before fixes. Boundary negative tests first missed forbidden controls,
then passed after the scoped checker was implemented. They reject native headers,
types, platform conditionals, private provider calls and unsupported server/UDP
APIs in the listed consumer sources, including private CMake dependencies. This
is a scoped source/CMake guard, not a complete C parser or all of Issue #24.

Host and native real-stack cases share the portable sample and independently
written runtime-shaped TCP adapter. Three cycles cover TCP/connected UDP echo,
TCP/UDP silent-peer timeout, early TCP EOF, oversized/zero-length UDP rejection
by the fixed-32-byte example protocol, runtime exchange and fresh TCP/UDP after
errors. Separate cases prove valid bidirectional zero-length UDP, refusal and
send after peer closure. Host composition explicitly ignores SIGPIPE: the actual
post-close send returns EPIPE on attempt 2 without terminating the process.
Native returns ENOTCONN on attempt 1. Host descriptor counts return to their
starting value after each scenario and after the suite (the count includes
inherited test-runner descriptors, so its absolute value is not a capacity).

Native carrier emulation has a live listener, observes the public link DOWN,
records zero application bytes and a network-down/unreachable or timeout result,
then restores carrier and proves fresh TCP and UDP success. The recorded run
returned ETIMEDOUT to the client and ENETDOWN from the private listener's accept.
This is not physical cable-loss evidence. Both real-stack configurations use
127.0.0.1 with loopback routing and loopback primary observation; netif does not
bind sockets. The cross-built RT1060 composition assumes isolated static
192.168.77.2/24 to peer 192.168.77.1, no gateway/DHCP/DNS, ports 42420/42421 and
the existing M3 PHY overlay/generated MDIO correction. See the contract before
interpreting or operating that configuration.

The native map resolves `close` to Zephyr `device_io.c`. Picolibc, live context
assertions and defined native socket symbols distinguish these calls from
Linux libc. A private declaration-only injected header supplies pinned Zephyr's
missing narrow-config `close(int)` declaration with C++ linkage protection; it
exports no native include path or socket implementation. Broad POSIX_API is not
enabled. Required narrower POSIX options enable some additional native
facilities, whose APIs are not thereby added to the consumer profile.

## Resource limits and qualification bounds

| Resource | native_sim executed | RT1060 cross-build only |
| --- | --- | --- |
| Contexts/connections | 6 / 6 | 4 / 4 |
| RX/TX packets | 16 / 16 | 16 / 16 |
| RX/TX buffers | 64 / 64 | 32 / 32 |
| Main stack | 4096 B | 4096 B |
| Test/peer stacks | 4096 B / 4096 B | Not applicable |
| TCP TIME_WAIT configuration | 1500 ms | 1500 ms |

Native tests require zero contexts and fully restored packet/buffer pools after
each sequential scenario and at the final point, allowing up to 500 ten-ms test
waits. Maximum observed settling was 1520 ms. An initial unpaced run exhausted
contexts: immediate reclamation, burst reconnect capacity, throughput and
long-run leak freedom are not qualified. Holding six native UDP sockets makes
the seventh return ENOENT (native errno 2); closing all six restores resources
and fresh allocation succeeds. This provider-specific errno is not a portable
exhaustion promise. Capacity and cleanup/timing on physical RT1060 remain
unverified. No capacities or cleanup test limits were increased to obtain PASS.

Deadlines bound retries in the application algorithm; they do not establish
worst-case scheduler, close/allocation, interrupt or hardware timing. No general
clock framework is introduced. SO_ERROR clear-on-read parity is not promised.
Host non-loopback carrier and address duplicate/tentative exclusion are limited
by getifaddrs as documented. TCP servers, unconnected UDP, runtime address
changes, multi-interface management, DNS/IPv6/TLS, arbitrary socket options,
complete third-party runtime compatibility and production security remain out
of scope.

## Retained failures and corrections

- The early native_sim external-libc probe and first stack run used Linux socket
  symbols despite enabled POSIX options; the latter died with SIGPIPE. They are
  not Zephyr evidence. Embedded Picolibc and native allocation assertions correct
  the fixture, and the proposal's early interpretation was corrected.
- Kconfig diagnosed a recursive select dependency. Explicit required POSIX
  configuration replaces recursive selection without broad POSIX_API.
- A clean native build exposed missing generated heap headers; netif now depends
  explicitly on `zephyr_generated_headers`. The narrow close declaration issue
  uses the private header described above, reviewed independently.
- Initial analysis identified swappable parameters, a reserved feature macro and
  possibly uninitialised time after failed clock calls. Parameter types/order,
  private CMake feature definition and EIO fallback corrected them; final full
  analysis and deterministic regressions pass.
- Independent review reproduced delayed-readiness expiry and runtime poll EINTR
  errors. Separate RED tests now pass with the original overall deadline intact.
- Carrier fixture expectations originally assumed only listener poll timeout;
  native accept can instead fail ENETDOWN. It now distinguishes poll failure,
  asserts zero received bytes and accepts the documented native network errors.
- The unpaced native run exposed TIME_WAIT context retention. The final fixture
  observes reclamation between cases and explicitly tests exhaustion/recovery;
  it does not hide this capacity limitation or weaken zero/full-pool assertions.
- Two earlier full Host runs hit existing 30-second Time-test limits. Unchanged
  individual reruns passed in 0.03/0.12 seconds; the cause was not established.
  Final complete debug and sanitizer suites pass without raising timeouts.
- Intermediate build failures from concurrent edits to test targets and a missing
  Host idle-resource callback were corrected before the final full runs.

Raw RED/failed and superseding GREEN logs remain in the evidence bundle. Earlier
partial pass counts and RT1060 sizes are superseded by the final table.

## Independent review

The read-only Reviewer did not implement changes and inspected actual source,
contracts, tests and raw logs. Initial deadline/poll findings were reproduced
with failing tests and corrected; re-review accepted the implementation and
resource-fixture corrections. Final independent verdict: **PASS at source 3cbf9a6, no unresolved material
findings**. The Reviewer verified 244 source hashes against Git and working
files, all 141 retained evidence hashes, archive contents and archive SHA-256.
This review accepted the software gate only; it did not judge physical traffic,
PHY behavior, target runtime capacity/timing, production security, comprehensive
runtime compatibility or sustained concurrency. Human review and Draft PR
integration remain separate from this independent software review.

## Outstanding physical acceptance

Issue #28's representative physical RT1060 controller communication path remains
pending. Cross-compilation cannot establish actual TCP/UDP traffic, PHY/carrier
loss, resource/timing behaviour or recovery. A separately reviewed, explicitly
authorised physical procedure with identified firmware, peer and raw evidence
is still required. Existing accepted M3 evidence is retained unchanged and is
not relabelled as portable M5/physical POSIX qualification.
