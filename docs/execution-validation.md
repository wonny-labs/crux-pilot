# Portable execution validation

Scope: [Issue #25](https://github.com/wonny-labs/crux-pilot/issues/25), on the
isolated `feat/m5-execution` branch based on `94b11e4`. The
[approved contract](execution-contract-proposal.md) records the POSIX comparison,
consumer requirements, alternatives and human approval. This record supplements
that contract with implementation details and qualification limits.

## Implementation and consumers

`components/execution/include/crux/execution.h` owns the C17/C++17 public API.
Applications link `crux::execution`; the composition selects the Host or Zephyr
provider. Common code owns checked generations, bounded reservations, join ownership
and admitted-operation references. Private native functions own clocks, thread
objects and synchronization. No provider registry, public allocator or OSAL is added.
Generation exhaustion permanently retires a slot. Handle storage is caller-owned;
copied aliases do not extend a lifetime. Stop publishing handles before destruction.

The qualification profile has four worker slots, eight mutexes and eight semaphores.
Zephyr Kconfig owns these capacities and the 4096-byte usable static stack request.
High/normal/low map to priorities 4/5/6; builds reject insufficient preemptive priority
levels. Host uses normal pthread scheduling for all classes. Its current requested
stack limit is 65536 bytes and each pthread receives 65536 bytes. These are supported
qualification configurations, not product resource budgets or scheduling equivalence.

The [firmware fixture](../tests/execution_firmware/) builds separately as a C/C++
consumer and performs a worker update, mutex transfer, notification, join and cleanup.
The [independent runtime fixture](../tests/execution_runtime/README.md) translates
runtime-shaped stack/priority/status values, runs twelve sessions and four bounded
reservation cycles, handles timeouts and reclaims quiescent resources. Its author
used the approved contract without provider implementation source. Portable sources
receive public include roots and ABI/toolchain flags only. Native runners and private
admission tests are separate test composition code, outside that portability claim.

## Reproduce without hardware

Use the documented Linux Dev Container, pinned west modules and Zephyr SDK:

```sh
scripts/check-host.sh
CRUX_HOST_PRESET=host-sanitize scripts/check-host.sh
scripts/test-portable-execution.sh
CLANG_FORMAT=clang-format scripts/check-format.sh
scripts/analyze-host.sh
scripts/check-dependencies.sh
python3 -m unittest discover -s tests/build -v
```

`test-portable-execution.sh --host-only` independently configures and runs both
consumers and checks their compile and link boundaries. The full command also runs
Zephyr `native_sim/native/64` conformance/runtime cases and cross-builds those
compositions for `mimxrt1060_evk@B//qspi`. Host and Zephyr workflows invoke these paths.
A west workspace with several Crux checkouts must explicitly select this module;
the script uses `ZEPHYR_MODULES` with pinned CMSIS/NXP dependencies to avoid duplicate
module discovery. Cross-builds never program, reset or communicate with hardware.

A shared GoogleTest source cache may be reused, but each build needs writable build
artifacts. This run used per-profile `CRUX_CACHE_DIR` directories and an explicit
`FETCHCONTENT_SOURCE_DIR_GOOGLETEST` pointing to the pinned cached source after the
shared cache contained root-owned object files. That failed build is retained; it
is an environment permission failure, not a passing check.

## Evidence and coverage

The final tested implementation is `b9df57dfefba53c938a3cddbb7f8b139b7986ab8`,
rebased onto `32405ad` after PR #35 was merged independently. Its Flash PoC files
and CI step are preserved. The before/after source and pinned-workspace captures
match exactly; the tested source was clean and recoverable from Git. Subsequent
changes add documentation/evidence only.

| Check | Final result |
| --- | --- |
| Host debug and sample scenario | 45/45 CTest cases; scenario passed |
| Host ASan/UBSan and sample scenario | 45/45; scenario passed |
| Separately configured Host firmware C/C++ and runtime consumers | 1/1 each |
| Zephyr `native_sim/native/64` | 14/14 cases, 2/2 configurations; no warnings |
| EVKB B/QSPI conformance/firmware and independent runtime | Both cross-builds and compile-boundary checks passed |
| Build safeguard/negative fixtures | 160/160 Python unit tests |
| Formatting, dependency checks, configured Host analysis | Passed |
| Independent implementation re-review | PASS for software PR preparation |

The [retained evidence](evidence/m5-execution/README.md) includes normalized raw
outputs, negative results, configurations, compressed maps, selected compiler commands
and source/artifact hashes. Original local logs are under
`build/evidence/m5-investigation/`. The reproduction environment used Ubuntu 24.04
Linux amd64 under Docker on an arm64 host, Clang 18.1.3, CMake 3.28.4, west 1.4.0,
Zephyr v4.4.0 and Zephyr SDK 1.0.1 / Arm GCC 14.3.0. Host results are functional
emulated-Linux evidence; no native production Linux or physical target claim follows.

| EVKB composition | Linker FLASH | Linker RAM |
| --- | ---: | ---: |
| Conformance and firmware fixture | 66,376 B | 31,168 B |
| Independent runtime fixture | 53,576 B | 31,040 B |

Each image allocates 16,640 bytes for four worker stacks (4,160 bytes per slot,
including native overhead for 4,096 usable bytes). Thread records occupy 704 bytes,
mutex records 320 bytes, semaphore records 256 bytes and metadata mutex 20 bytes
in these ELFs. Whole-image figures include different test runners and do not
measure wrapper overhead. Simulated trivial-worker observations report 4,096 usable
and 4,056 unused bytes at priorities 6/5/4; they do not establish firmware/runtime
worst-case headroom or physical scheduling behavior.

One integrated Debug run reported a 30-second process-exit timeout for the unchanged
`TimeCommandTest.NoDataApiStatusMapsToDistinctCliText`. Its assertions and GoogleTest
teardown reported success before exit stalled. That executable does not link the
execution provider. Twenty direct repetitions and subsequent sequential complete
Debug/sanitizer runs passed. The failed run and its details remain retained: the
cause is unresolved, and no Time source was changed or timeout increased. Hosted
CI is separate evidence and is not inferred from these local results.

Common conformance exercises lifecycle/visibility, invalid input, pool exhaustion,
reuse/stale aliases, finite expiry without early return, timeout retention, self-join,
mutex ownership/exclusion/wakeup, semaphore full/empty/wakeup and simultaneous gives.
Private linker wrappers pause an operation **after admission** and prove concurrent
join rejection and busy destruction with admitted semaphore/mutex operations, followed
by completion and cleanup of the original lifetime. These wrappers are linked only
into tests and introduce no production hooks or public native access.

Host tests inject native creation failures and deadline overflow, check rollback
and repeat full-capacity allocation. A deterministic poll regression holds the real
semaphore bookkeeping lock while two tokens remain available. It failed with BUSY
before the correction; polling now serializes bookkeeping and consumes a token.
`native_sim` also checks every public operation rejects ISR entry and observes each
priority class plus native usable/unused worker stack. Observations are test-specific
headroom, not worst-case stack or physical timing measurements.

The EVKB public compile probe rejected missing Thumb/CPU requirements before the
build fix. The public target now propagates Zephyr's selected `TOOLCHAIN_C_FLAGS`
(CPU/instruction set/ABI/sysroot), while generated configuration, native headers and
kernel definitions remain private. Compiler commands and linked objects must be
retained alongside maps and generated configuration.

The additional POSIX investigation found that monotonic condition attributes are
accepted on this Zephyr pin, but a timed-out condition wait fails to restore mutex
ownership: the subsequent unlock fails. Source inspection confirms
`kernel/condvar.c` reacquires only on successful wake and the POSIX adapter converts
the timeout without relocking. Linux passed the same timeout/wakeup probe. This
qualifies the proposal's standards-level alternative: monotonic POSIX condition
variables are a valid design route, but this pinned implementation does not satisfy
that timeout ownership requirement. No upstream fix or toolchain upgrade is included.

## Controller migration seam and unresolved dependencies

The existing M2/M3 controller retains its composition-local native lifecycle,
phase-grid release and skip-overrun policy. A later migration can replace native
worker create/join, mutex operations and bounded signals with this contract. It
must preserve Time ownership and priority/stack assumptions. Public clock/sleep,
periodic release, queues and events belong to #26; this change does not silently
port those mechanisms or rewrite historical M2 evidence.

[Issue #24](https://github.com/wonny-labs/crux-pilot/issues/24) remains open. This
change checks designated execution public/consumer sources, forbidden native
headers/types/calls and compile/private-path leakage, with negative fixtures and
public-target link records. It permits controlled providers. It does not deliver
#24's general dependency policy, complete link-graph enforcement or all M3 migration
mapping. That shared integration dependency must be resolved before claiming the
broader native-free platform boundary.

No EVKB hardware operations are authorized or performed. Cross-builds and simulated
stack observations cannot satisfy #25's physical execution/resource or timing
acceptance. Physical target evidence remains an explicit follow-up gate for Issue
completion. This PR may be reviewed with that limit; it must not close #25 or imply
production runtime compatibility, realtime guarantees, fairness, bounded priority
inversion, cancellation, stuck-worker recovery or long-duration reliability.
