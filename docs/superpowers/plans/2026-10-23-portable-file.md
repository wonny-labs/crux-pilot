# Portable File implementation plan

> **For agentic workers:** Use the approved contract and TDD task-by-task. Root coordinates independent Reviewer and Challenger gates.

**Goal:** Implement Issue #23's approved software-only File capability.
**Architecture:** Small shared validation/lifetime layer, private provider port,
Linux Host provider, deterministic private fault provider. Native VFS mapping is
documented; no writable QSPI composition is enabled.
**Tech Stack:** C17, C++17 consumer, CMake/CTest, existing Linux Dev Container.
**Spec:** [Approved File contract](../../file-contract.md).

## Global constraints

- Preserve approved operations, positive PARTIAL, exact byte counts, zero-progress
  handling, consume-on-close, primary truncate error and no rollback semantics.
- Keep native types/headers and test controls private; public target `crux::file`.
- No production Flash layout/driver, upstream edit, hardware, merge or Issue close.
- Result enums are distinct domains; document explicit named comparisons and no
  implicit casts to `crux_status_t`. Unknown native errors map to File IO.

## Review focus

Failed close cannot retry released native state; failed create/truncate cleanup
must close once while preserving the primary error. Short writes, zero-progress,
unknown errno and exhausted handles cannot report full success. Invalid/open
outputs, representability, read-only modes and path bounds must be tested. Retain
native-free consumer checks while isolating trusted composition initialization.

### Task 1: Contract, shared layer and deterministic fault tests

Files: `components/file/include/crux/file.h`, `components/file/src/file.c`,
`components/file/src/file_port.h`, `components/file/CMakeLists.txt`,
`tests/file/` and root `CMakeLists.txt`.

- [ ] Add declarations and tests first; run RED against inert/missing behavior.
- [ ] Implement validation, ownership, pool/lifetime and private port only as
  needed for GREEN; all provider/test internals remain outside public includes.
- [ ] Cover every declared operation plus short/zero read/write, explicit full,
  failing sync/close, invalid arguments, permission, seek overflow/beyond EOF,
  pool exhaustion and failed-create/truncate cleanup. Keep test controls private.
- [ ] Record commands and RED/GREEN evidence; self-review the coherent task.

### Task 2: Host provider and independent consumers

Files: `components/file/host/`, `tests/file_consumer/`, and File test definitions.

- [ ] TDD the private Linux Host provider against isolated temporary directories.
  Require explicit trusted composition root selection; fail NOT_READY otherwise.
- [ ] Test real create/write/sync/seek/read/close/reopen/remove, error mapping and
  resource reclamation. Use fault provider for deterministic failure conditions.
- [ ] Configure separate C17/C++17 consumers linking public targets only. Keep
  composition root binding in a distinct native fixture from portable consumers.
- [ ] Extend existing #24 usage/direct-consumer guards, not generic exceptions.

### Task 3: Verification, VFS handoff and Draft PR evidence

Files: `docs/file-contract.md`, `docs/file-validation.md`, bounded evidence under
`docs/evidence/file/`; existing CI changed only if an uncovered focused check needs it.

- [ ] Run supported Linux Host/debug/sanitizer, dependency/format/build-tool checks
  and independent consumers. Add proportionate checks to existing CI as needed.
- [ ] Specify VFS open-without-TRUNC, separate truncate/one-close failure mapping,
  close retirement and range checks. Any shim/compile evidence is labelled;
  it cannot stand in for actual Zephyr VFS/runtime evidence.
- [ ] Map all six #23 AC with exact source/test/evidence identities and limits.
- [ ] Obtain independent Reviewer and Challenger assessments, resolve material
  findings and prepare a focused Draft PR. Root handles publishing and attachment.
