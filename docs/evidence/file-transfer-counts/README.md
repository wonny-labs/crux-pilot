# File transfer count integrity

This separate Cloud task hardens the portable File/provider boundary. It does
not reconstruct the unavailable Mac M4-1 checkpoint or change Scratch/XIP work.
No physical operation, target admission, production adoption, push, PR creation,
merge or issue closure is included.

## Baselines and selected gap

Main: `08d7ccda699532892c96147742dc9355afdcfcd0`.
Published simulator candidate #56: `b31f4f05c053c8141543db9ac1e9c3d2620b16eb`.
The candidate was tested in a detached, disposable worktree; its published branch
was not changed. Issues #14/#15/#16/#23 and Draft PRs #51/#56/#59/#62/#63/#66 were
reviewed. Existing VFS/LittleFS lifecycle and Settings/NVS CRC/fault evidence is
reusable within its documented scope; it does not qualify physical persistence.

Both main and the published candidates trusted private provider transfer counts.
A provider reporting nine bytes for an eight-byte request could escape as full
success. This is a fault-provider reproduction, not a discovered LittleFS defect.
The common layer now rejects any count above the request with IO and zero count,
including contradictory error/PARTIAL results. Valid counts on errors, short
reads, partial writes, handles and locking retain their existing semantics.
Reporting zero does not prove that buffers/storage were unchanged or roll back
any effects. The guard cannot prevent a provider from overwriting memory.

The change is intentionally independent of candidate selection and native
patch adoption. Configuration remains unchanged. Native integration and exact
image/hardware acceptance retain their existing dependencies and NO-GO gates.

## TDD and validation

`red.txt` records the new READ overcount assertion failing against unchanged
main source (exit 134). The deterministic provider no longer clamps its injected
count. The matrix exercises READ/WRITE, counts `length + 1` and `SIZE_MAX`, and
OK/PARTIAL/NO_SPACE/IO results. Existing checks still require two accepted bytes
to survive NO_SPACE and exercise normal EOF/partial/lifecycle behavior.

Final `verification.json`: **54 successful compile/link/run commands**, including
**16 executable runs**: fault contract, Linux provider and public-only C17/C++17
consumers, each in debug and ASan/UBSan modes, on main and detached #56.
GCC/G++ 14.2.0; Python 3.13.5; strict warnings as errors. Consumer compilation
receives only `components/file/include`, not provider-private includes.
The exact Cloud reproduction script is retained as `verify.py`; run with:

```sh
ASAN_OPTIONS=detect_leaks=0 /usr/bin/python3 docs/evidence/file-transfer-counts/verify.py
```

It uses the original `/workspace/crux-pilot`, `/workspace/crux-candidate56` and
`/workspace/crux-results` locations. Adapt those paths for another workspace.
The #56 worktree contains only the same four-file change. Its core/test/docs
hunks apply with reduced context; its header comment was transferred separately
because #56 adds sleepable-lock declarations immediately before that comment.
Those declarations and lock implementation are preserved.

Additional checks:

- `python3 -m unittest discover -s tests/build -p test_native_interfaces.py -v`:
  **18 PASS**, no skips (`native-interface-tests.txt`).
- `scripts/check-dependencies.sh`: **PASS**, 76 designated portable sources plus
  existing execution/networking source boundaries (`dependencies.txt`).
- GCC `-std=c17 -Wall -Wextra -Wpedantic -Werror -fanalyzer` on the common File
  source with its public/private includes: **PASS**, no diagnostics.
- `git diff --check`: **PASS** on both worktrees.

`source-hashes.json` identifies the tested four-file source delta.

## Failures and unexecuted checks

The initial sanitizer attempt failed because LeakSanitizer cannot run under the
environment's tracing (`verification-initial.json`). The final runs explicitly
disable leak detection; ASan/UBSan passed, **leak checking is not claimed**.
An intermediate candidate build used main's renamed `file_linux.c`; #56 retains
`file_host.c`. That failed command is preserved in `verification-path-failure.json`.
The final runner selects the actual candidate filename and lock source.

CMake, west/Zephyr dependencies, the SDK and pinned clang-format were not prepared
in this environment; a tools bootstrap attempt did not complete. Full Host CTest,
CMake usage-target graph tests, pinned formatting, Zephyr/native_sim and ARM
builds were **not run**. Direct compiler checks are not substitutes for those
gates. No native provider, build selection, public declaration, or physical path
changed. The production and simulator source guards remain untouched.

## Independent review

Reviewer (`/root/reviewer`): **PASS**, no material findings. Independently inspected
contracts, actual diff, RED artifact and #56 adaptation, and ran the main fault
suite with strict GCC C17 plus ASan/UBSan (leak checking disabled). The guard
rejects contradictory counts while preserving valid error counts; documentation
correctly avoids rollback claims. Scope is portable behavior and candidate Host
compatibility, not native runtime or physical qualification.

Challenger (`/root/challenger`): **PASS**, no material findings. Independently
inspected source/tests/contracts and published #56/#66 transfer semantics, ran
the strict main fault suite and UBSan, and checked whitespace. No sleepable-lock,
native-provider, Scratch/XIP, layout or admission change was found. Direct public
header compilation does not establish CMake usage-boundary or Zephyr execution
qualification. These engineering reviews grant no hardware or integration approval.

Both reviewers subsequently checked the final 54-command/16-run matrix and
matched all four retained source hashes. Both confirmed **PASS** with no material
evidence inconsistency; unchanged tests were not unnecessarily repeated.

## Independent next work

A software-only runner for the currently manual simulator-provider gates can be
developed separately, using pinned sources and explicit writable outputs. Actual
VFS/LittleFS and Settings/NVS regressions should then be run against a reviewed
current-main integration. Neither requires the Mac's unpushed Scratch/XIP work;
neither may enable target admission or claim M4 physical acceptance.
