# Portable Configuration implementation plan

> **For agentic workers:** Use superpowers:executing-plans with TDD. Root coordinates independent Reviewer and Challenger gates.

**Goal:** Implement Issue #16's approved portable scalar behaviour and software evidence.
**Architecture:** Caller-owned BOOL/U32 parameter state and application descriptors,
canonical encoding, private persistence port, deterministic Host fault provider.
No backend-dependent state or namespace leaks into public consumers.
**Tech Stack:** C17, C++17, CMake/CTest, existing Linux Dev Container.
**Spec:** [Approved Configuration contract](../../configuration-contract.md).

## Global constraints

- Preserve defaults, revalidation, accepted-versus-runtime-versus-persisted
  semantics, canonical encoding and explicit missing/rejected/IO distinctions.
- No raw-NVS substitution, production Settings/NVS adoption, upstream/Flash edits,
  writable layout, hardware, PR merge or Issue closure.
- Results are separate named domains from `crux_status_t`; no implicit casts.
- Native provider acceptance remains blocked if Settings hides relevant errors.

## Review focus

Mutable validation context cannot publish an invalid fallback or persist an invalid
current value. Short transport reads are IO, not missing/corrupt success. IO keeps
previous accepted state and save errors do not imply unchanged persisted bytes.
Encoded bytes cannot depend on padding/endianness. Public target/consumer graphs
must remain native-free; schema and controller activation remain application-owned.

### Task 1: Shared scalar contract and private fault-backed tests

Files: `components/configuration/include/crux/configuration.h`,
`components/configuration/src/configuration.c`, private `configuration_port.h`,
`components/configuration/CMakeLists.txt`, `tests/configuration/`, root CMake.

- [x] Add declarations and failing tests for init/read/update first; verify RED.
- [x] Implement minimal validation and lifetime behaviour, then verify GREEN.
- [x] Add failing save/load and canonical byte assertions before implementing
  their shared mechanism. Keep provider port out of public include paths.
- [x] Test missing/malformed/type/range/version/short/failed reads; unknown native
  error handling stays at private provider boundary. Check default/context changes,
  zero IO on rejection, failed save after modeled media change and retry/reload.

### Task 2: Independent consumers and controller-owned example

Files: `tests/configuration_consumer/`, optional
`samples/controller/configuration/`, root CMake usage guards.

- [x] Build/link/run independent C17/C++17 fixtures with a second application
  schema, separate native/fault composition and no GoogleTest dependency.
- [x] Add the optional controller-core scenario with application descriptors and
  explicit cycle-boundary runtime snapshots; preserve historical M3 behaviour.
- [x] Register designated consumers/public targets with existing #24 guards.

### Task 3: Software verification and bounded native handoff

Files: `docs/configuration-contract.md`, `docs/configuration-validation.md`,
`docs/evidence/configuration/`; existing CI only where focused coverage is absent.

- [x] Run Linux debug/sanitizer, source/build boundaries, formatting/static checks
  and independent C/C++ consumers. Compile shared C17 code in applicable Zephyr
  software-only composition without enabling Flash/Settings/NVS initialization.
- [x] Retain source/isolated Settings wrapper fault evidence with honest shim
  limits. Do not claim it satisfies actual Settings/NVS provider or physical AC.
- [x] Map all ten #16 AC as software-satisfied, partial or pending with reasons.
- [x] Reviewer/Challenger inspect final source and evidence; resolve findings;
  prepare Draft PR, leaving native/physical/M4 completion gates intact.

Evidence note: final implementation/review results and the Issue #16 partial
acceptance map are in `docs/configuration-validation.md`. Native/physical work
remains gated. The optional controller scenario uses the existing core; mixed-key
partial-save reconstruction is covered separately by the conformance harness.
Draft publication is the final coordinated step, not merge/closure authorization.
