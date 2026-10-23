# Portable Configuration software qualification

Issue [#16](https://github.com/wonny-labs/crux-pilot/issues/16) now has the
[approved scalar contract](configuration-contract.md), shared C17 implementation,
a private deterministic persistence port and independent C17/C++17 consumers.
This is the software portion of the issue. There is **no adopted native Settings
provider, persistent production backend or physical qualification** in this change.
The storage direction in [#14](https://github.com/wonny-labs/crux-pilot/issues/14)
remains QSPI NOR/LittleFS plus separate Settings/NVS as a design candidate.

## Implemented behavior

Consumers include `<crux/configuration.h>` and link `crux::configuration`.
The caller owns zero-initialized parameter objects and immutable descriptors.
Each descriptor owns the key, schema version, BOOL/U32 default and optional
application validator. There is no allocation, global schema, migration engine,
worker, implicit save or automatic repair. Objects, context and conflicting key
accesses require caller serialization; latency and ISR use are not supported.

Init and update validate before publication. Load decodes into a temporary value;
only a valid record or freshly revalidated default changes accepted state.
Explicit missing returns DEFAULT_MISSING; a complete invalid record or reliably
invalid length returns DEFAULT_REJECTED. Failed or short transport reads return
an error and preserve accepted state. An invalid fallback remains an error and
preserves the prior value. Save revalidates before dispatch; a failed save leaves
accepted state unchanged but may have changed stored bytes. The fault provider
models both unchanged and changed storage on failure; it does not model actual
NVS crash recovery.

Canonical records contain version/type/schema followed by one BOOL byte or four
little-endian U32 bytes (5/8 total). No structure padding or host endian data is
persisted. Named capability results are separate from `crux_status_t`: zero is
full success, negatives are errors, and positive fallback outcomes require
explicit handling. Numeric casts or generic success/nonzero-error conversion
would lose meaning and are not provided.

The private port distinguishes record length from actual transferred bytes.
Only NOT_READY, IO, NO_SPACE and NOT_SUPPORTED are valid negative transport
results; unexpected provider results become IO. An adapter must prove absence,
not infer it from a failed scan or short read. Applications can supply their own
composition without exposing provider headers/types to consumers.

## Application activation example

`samples/controller/configuration/scenario.c` uses the existing portable
controller core and application-owned `report_every_cycles` (default 5, range
1–100) and `diagnostics_enabled` (default false). It demonstrates missing defaults,
read/update/save/load, rejected updates, and a runtime snapshot captured at cycle
4. Updating accepted values does not alter the previous snapshot. Across twenty
observations, the example checks reporting/diagnostic policy while the original
100 ms schedule remains unchanged. Recreated parameter objects reload both
saved values; later unsaved updates are not restored.

This deterministic Host scenario uses an in-memory fault provider. It is not a
new native controller loop, physical reboot or persistence test. The two separate
saves do not promise a multi-key transaction or consistent recovery snapshot. A
separate deterministic fault case saves the new first key, fails the second save
before changing it, reconstructs both objects and observes the valid mixed-version
pair. Application cross-key validation/activation remains its own responsibility.

## Settings/native integration gate

Pinned Zephyr: `684c9e8f32e4373a21098559f748f06915f950c9` (4.4.0).
`tests/native/settings_store/probe.py` compiles an unmodified copy of the pinned
`settings_store.c` with explicit Host shims and controlled store callbacks.
The [result](evidence/configuration/settings-store-probe-result.json) includes
source/header/harness hashes, twelve observations and a rejected oracle mutation.
It confirms:

- subtree/direct wrappers can discard traversal errors;
- `settings_load_one` can return advertised length 8 after an actual 2-byte read;
- a later source can mask an earlier error;
- `settings_save_one` propagates modeled save errors, while subtree save ignores
  start/end lifecycle errors.

The list, locking and handler enumeration are shims. This is source-wrapper
behavior, not actual Zephyr/NVS execution, media durability or concurrency proof.
The modeled changed flag only proves how the contract handles uncertainty.
Pinned `settings_nvs_load` also needs investigation of negative scan probes and
ignored repair/delete results before a reliable absence/error claim is possible.

**Affected native integration is stopped.** Do not wire the approved scalar
contract to these wrappers and silently treat failure as missing. The next focused
proposal must identify an owned Settings integration path that preserves source
traversal and actual callback-read errors/counts, plus backend fault tests for
NVS scan/repair errors. Any upstream patch/maintenance ownership or alternative
backend requires human review. Direct raw-NVS substitution is not approved.

Recommended next decision: approve a separately bounded, copied-source Settings
remediation experiment before native adapter implementation. Preserve the first
source traversal error, carry actual callback transfer length, and distinguish
NVS scan failure from confirmed absence. Start with `settings_save_one` for the
per-parameter write path rather than subtree lifecycle batching, but still verify
actual NVS write/recovery errors. Reuse deterministic fault controls; do not adopt
such changes in pinned upstream or production until patch ownership and native
qualification evidence are separately reviewed. This is a proposal, not an
implemented adapter or an extension of current authorization.

Production layout, image/erase enforcement, complete XIP dependency closure,
bounded native Flash failure behavior and physical validation retain the gates
recorded by [Flash safety acceptance](flash-safety-acceptance.md) and #14/#15/#16.
No Flash driver, partition, mount, Settings initialization or physical tool is
changed or exercised here.

## Acceptance map

| #16 criterion | Software evidence / status | Remaining gate |
| --- | --- | --- |
| 1. Reusable contract and independent consumer | Satisfied for approved BOOL/U32 profile: public header, C17/C++17 consumer fixtures with a second schema, source and CMake boundaries | Future types require a separate contract decision |
| 2. Controller defaults/read/update/save/load and application timing | Satisfied in deterministic Host scenario using actual portable core and cycle-boundary snapshots | Native composition and actual deployment remain pending |
| 3. Type/range rejection preserves active/persisted values | Satisfied in conformance tests: rejected update/save performs no write; accepted value preserved | Application owns cross-key/runtime validation |
| 4. Missing defaults distinguished | Satisfied: explicit absence vs invalid record vs transport failure, no implicit save | Actual Settings/NVS absence reporting not qualified |
| 5. Malformed/truncated/range data not activated | Satisfied in shared tests: complete invalid data defaults explicitly; short transport preserves prior state with IO | Actual backend record/read behavior pending |
| 6. Observable load/save failure and recovery semantics | Partial: deterministic NOT_READY/IO/NO_SPACE/short/unknown results, failed save after modeled change, retry/reload | Native Settings/backend error propagation blocked as above |
| 7. Physical restart restores parameters | Not demonstrated | Explicit hardware authority, approved backend/layout and safety prerequisites |
| 8. Interrupted save/restart without multi-key atomicity promise | Partial: error-after-change and reconstructed objects/multiple keys in memory | Native interruption/recovery and physical restart evidence |
| 9. Portable Host flow and relevant Zephyr verification | Partial: Host flow/consumers and shared-mechanism Zephyr software fixture | Actual Settings adapter and native persistence integration |
| 10. Actual backend/layout/readiness/integrity/physical limitations | Partial: software bounds/port/encoding and explicit exclusions documented | Production configuration selection, native readiness and physical evidence |

Issue #16 and M4 remain open/incomplete. This PR does not fulfil their native or
physical acceptance criteria.

## Reproduction and evidence

Use the documented Linux Dev Container. Standard Host debug/sanitizer CTest runs
include conformance and the controller scenario. Host CI's existing build-tool
unittest discovery also builds/runs the independent C/C++ fixtures; no new CI job
or framework is added. Source/native usage boundaries remain mandatory.

```sh
scripts/check-host.sh
CRUX_HOST_PRESET=host-sanitize scripts/check-host.sh
python3 -m unittest discover -s tests/build -v
scripts/check-format.sh
scripts/check-dependencies.sh
scripts/analyze-host.sh
clang-tidy-18 -p build/linux/debug components/configuration/src/configuration.c \
  tests/configuration/conformance.c tests/configuration/fault_provider.c \
  samples/controller/configuration/scenario.c
```

Additional focused manual gate for changes to this mechanism or pinned Settings
source (not a replacement for standard CI):

```sh
python3 tests/native/settings_store/probe.py --zephyr "$ZEPHYR_BASE" --output /tmp/settings-result.json
cmake -S tests/configuration/zephyr -B build/configuration-native -GNinja \
  -DBOARD=native_sim/native/64 -DZEPHYR_MODULES="" -DZEPHYR_TOOLCHAIN_VARIANT=host
cmake --build build/configuration-native
build/configuration-native/zephyr/zephyr.exe
```

The Zephyr fixture compiles the shared C17 algorithm plus deterministic provider
in a ztest app. It enables no Flash/Settings/NVS service and proves no native
persistence safety. Retained TDD, qualification and review records are under
[evidence/configuration](evidence/configuration/); the final run manifest identifies
source inputs and observed counts. The final conformance harness has 163
assertions (not 163 independent CTest cases). Linux debug and sanitizer regressions
passed 78/78 each; after the review-driven mixed-key test, affected Configuration
cases passed 2/2 in both builds and native_sim repeated all 163 assertions.
Build-tool unittests passed 293/293, including independent consumers and maintained
boundary fixtures. The initial environment-induced failure is retained with its
cause and correction; it is not counted as a successful run. Initial inert stubs failed the behavioral
checks; the controller stub and omitted consumer inventory likewise failed their
new checks before the corresponding implementation/registration.

Validation limitation: a redundant full Host rerun later left an unchanged Netif
child as a zombie while CTest waited in epoll. That run was stopped, preserved,
and is not counted as passing; the runner observation remains unresolved. Final
affected Configuration cases passed 2/2 per preset with explicit time limits,
and final targeted static analysis passed without new suppressions. Earlier full
78/78 debug/sanitizer results and the 293/293 build-tool run remain separate
observations. Require latest hosted CI before any merge decision.
