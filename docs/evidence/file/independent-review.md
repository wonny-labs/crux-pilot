# Independent portable File review

Verdict: **PASS — approved software-only #23 contract implementation and handoff.**
No material findings in the reviewed source/tests. This does not approve a
production filesystem/backend/layout, qualify physical storage, or authorise
merge/Issue closure. Root coordination still owns the final evidence/review gate.

Reviewed File implementation (code recorded by commit `3a52636`) in branch `feat/portable-file` in
`/reviewed-worktree` on the
recorded `225cd21` baseline. Reviewed `docs/file-contract.md`,
`docs/file-validation.md`, every file in `components/file`, `tests/file` and
`tests/file_consumer`, plus relevant CMake/source-boundary/Host CI changes and
retained verification records. No implementation or upstream edits were made.

## Required behavior checked

- Public enums preserve capability-specific named results. Native errno is
  translated in the Host provider; unknown errors become IO. Positive PARTIAL
  is confined to nonnegative short writes, including zero progress. Explicit
  write errors preserve any count already acknowledged by the private port.
- Open rejects a non-null output without losing it. Failed accepted opens leave
  the output null. The bounded handle pool recovers after close/truncate failure.
- CREATE_REPLACE opens without native TRUNC, then truncates separately. A failed
  truncate closes once, preserves the primary error, records a secondary cleanup
  failure, and does not publish the handle. Tests model changed file state and
  assert it is not rolled back.
- Close clears the public pointer, calls the provider once and retires the shared
  slot even when the provider returns failure. The fault store marks its native
  state retired and asserts against another native close. Subsequent independent
  opens remain possible. Stale aliases are expressly outside the contract.
- The Linux provider retires its descriptor before its single `close` call;
  interrupted calls are not automatically retried. No failed-close rollback or
  physical durability is implied by an earlier sync or write result.
- Offset/request conversion, EOF checks, zero-length handling, access mode,
  name limits, remove-while-open rejection and pool exhaustion are explicit.
  Pool serialization uses C17 atomics and includes blocking provider calls;
  the disclosed unbounded contention/serial-I/O limits match the contract.
- Native root selection and the private provider/test controls stay outside the
  public include tree. Independent consumers include public declarations only;
  root binding lives in a separate composition fixture.

## Independently executed verification

First, local strict C17 + ASan/UBSan compilation and deterministic contract
execution passed. The source boundary script passed for its 31 registered
portable source files in the observed invocation.

Then, using the existing `crux-file-qualification` Linux container with the actual
worktree at `/src`, independently built temporary binaries with strict warnings,
assertions enabled and ASan/UBSan:

| Executable | Result |
| --- | --- |
| Shared implementation + private fault provider | PASS |
| Shared implementation + actual Linux provider + Host test | PASS |
| Independent public-only C17 consumer + separate Host composition | compile/link/run PASS |
| Independent public-only C++17 consumer + C17 implementation/composition | compile/link/run PASS |

No sanitizer diagnostics occurred. Binaries were built in temporary directories;
the source worktree/upstream were unchanged. Temporary ordinary Host files were
used, with no devices, board tools or physical operations.

The actual Linux test includes CRUD/sync, two-record seek/read, EOF, replace,
read-only write rejection, invalid root/readiness, directory rejection, repeated
open/close and descriptor-count recovery. The deterministic test supplies partial
and zero-progress IO, storage-full/IO/sync/close/truncate failures and native-slot
retirement. These are software assertions, not physical persistence/recovery.

## Evidence and acceptance limits

The six #23 criteria are supported by the approved/reviewed contract, independent
Host consumers, deterministic failures, exact planned VFS mapping, explicit
unsupported/non-promised guarantees, and #15 handoff links. Final supported
CMake/CI and retained source-revision evidence remain root responsibilities;
the direct compiler rerun does not independently certify every retained build log.

The Zephyr mapping is correctly labeled a plan. Shared fault-port lifecycle tests
do not run `fs_close`, `fs_truncate` or LittleFS. Controlled actual VFS integration
must validate native initialization, failed-truncate cleanup, released-state
retirement and diagnostics before adopting that provider. Native mount/media/
format/recovery, production enforcement and physical #15 qualification retain
their separate safety and human decision gates.
