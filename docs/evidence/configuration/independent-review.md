# Independent Configuration implementation review

Verdict: **PASS for the inspected portable mechanism and focused Host evidence.**
This is not acceptance of all Issue #16 criteria, native Settings integration,
physical persistence, or the final PR. The final-delta re-review below extends
this verdict to the controller example and mixed-key software recovery evidence;
final retained build/CI verification remains root coordination work.

Reviewed worktree: `/reviewed-configuration-worktree`,
branch `feat/portable-configuration`, uncommitted implementation on the recorded
`225cd21` baseline. Reviewed the approved contract, implementation plan, public
header, shared implementation/private port, CMake declarations, conformance/fault
provider, and independent C/C++ consumer fixtures. No source or upstream files
were edited by this reviewer.

## Material findings

No material implementation defect found in the inspected portable mechanism.

- Caller-zeroed lifetime, rejected reinitialisation, failed-init preservation,
  descriptor/type/key validation and explicit per-capability results match the
  contract. Public headers use ISO C declarations and C++ linkage guards.
- Missing and invalid-record fallback revalidate defaults; changed validator
  context cannot publish a rejected fallback or cause an invalid save dispatch.
- Valid-length short reads and explicit provider errors preserve the accepted
  value. Invalid reliable lengths are distinguishable from incomplete transfers.
- Canonical BOOL/U32 encoding is explicit and independent of native structure
  padding/endian layout; noncanonical BOOL values and schema/type/version
  mismatches are rejected.
- Save errors preserve accepted memory but do not claim unchanged stored bytes.
  Unknown private-port outcomes map conservatively to IO. Test controls and the
  persistence port remain outside public include roots.

## Independently executed checks

- Direct C17 compile with `-Wall -Wextra -Wpedantic -Werror`, ASan and UBSan,
  followed by conformance execution: **138 checks, zero failures**; no sanitizer
  diagnostics. The temporary executable was removed with its temporary folder.
- Independent public-header-only C17 and C++17 consumer translation units,
  linked to C17 shared implementation and separate private test composition:
  **both compiled, linked and ran successfully**.
- `python3 scripts/check-native-interfaces.py`: **PASS**, 30 portable files in
  the observed invocation. Registration was being updated concurrently, so this
  count is not evidence of the final committed registry.
- Configuration consumer CMake test discovery: **skipped**, because CMake is not
  installed in this local environment. Direct compiler checks do not prove the
  public CMake usage graph; supported Linux/CMake verification is still needed.

## Remaining acceptance work and scope

The optional controller fixture has concrete value: it supplies #16 AC2 evidence
using the actual portable controller core and demonstrates that changing accepted
configuration does not silently change the runtime snapshot before an application
cycle boundary. A small Host-only scenario with the approved two descriptors,
save/load, rejected update and mixed-key restart is proportionate. It must retain
application schema/activation ownership and avoid changing historical schedules.
Its absence is an acceptance gap, not a reason to redesign the scalar component.

The current isolated restarted-object test is not complete mixed-key restart or
power-loss evidence. Do not claim the approved mixed-key scenario until tested.

Native Settings/provider AC6 remains intentionally incomplete: the shared fault
port proves portable failure semantics, not that the pinned actual Settings/NVS
path exposes every backend error. Existing wrapper probes retain their disclosed
Host-shim limitations. AC7/8 physical results and the actual physical support
record remain pending the approved provider, Flash safety prerequisites and
separate authority. No raw-NVS substitute or production backend is approved here.

Re-review the final controller/example changes, retained evidence and final
acceptance map before presenting a complete software PR result. No hardware
operations, production-layout changes, merge or Issue closure were performed.

## Final delta review

Verdict: **REVISE — one bounded missing software recovery scenario.** The shared
mechanism remains PASS; no implementation defect was found in the delta.

Independently reviewed the actual-core controller scenario, its Host composition
and CMake/native-free registration, the added conformance cases, Zephyr software
fixture, retained Settings wrapper probe and `docs/configuration-validation.md`.
Independently rebuilt and executed in the existing Linux container, using a
temporary build folder with strict C17 warnings and ASan/UBSan:

- Conformance: **147 checks, zero failures**, no sanitizer diagnostics.
- Actual-controller-core configuration scenario: **PASS**, no sanitizer
  diagnostics. The cycle-4 application snapshot, default 5/range 1–100,
  rejected update, later unsaved changes and reconstruction of saved values are
  correctly exercised. Existing controller schedule code is reused unchanged.

The final acceptance map properly keeps native AC6 and physical/native portions
of AC7–10 pending. The Settings wrapper probe retains its Host-shim/source-only
limits; it does not substitute for an actual Settings/NVS adapter.

### Material missing test

`samples/controller/configuration/scenario.c:54–55` saves both keys successfully;
its later reconstruction reloads the fully acknowledged pair. This demonstrates
two-key reload, but not the approved **mixed-key restart after partial save**
scenario. The conformance failure-after-change test still involves one key only.

Add a deterministic case with an old saved pair, new accepted pair, successful
first-key save followed by second-key failure-before-change, then reconstructed
objects observing the expected mixed old/new pair. Explicitly preserve the
absence of multi-key atomicity. A separate application snapshot validation step
may illustrate policy if useful, but no batch API or framework is needed.
This completes planned portable failure/recovery coverage; it does not qualify
native interruption or physical #16 AC8.

No source edits or hardware operations were performed by this reviewer.

## Final delta re-review — finding resolved

Final verdict: **PASS for the completed portable software scope and native
handoff, with the documented native/physical blockers retained.** This supersedes
the preceding bounded REVISE finding.

Independently read the added `mixed_key_cases()` and revised validation record.
The case starts with saved 5/false, accepts 7/true, saves the first key, injects
failure-before-change for the second, verifies accepted memory is still true,
then reconstructs/loads both objects and observes 7/false. It explicitly leaves
activation/consistency policy to the application and introduces no transaction
or native persistence claim.

Independently rebuilt/reran the final conformance source with strict C17 warnings
and ASan/UBSan in Linux: **163 checks, zero failures**, no sanitizer diagnostics.
The actual-core controller scenario had independently passed ASan/UBSan in the
preceding delta review and was unchanged by this focused test addition.

No remaining material review findings. The final evidence manifest/TDD summary
must use the final 163 count where claiming current conformance results; the
earlier 138/147 counts remain valid historical observations only. Native AC6,
physical/native AC7–10 and actual Settings integration remain pending as mapped.

## Static-analysis correction delta

Verdict remains **PASS** after independently reading the private record-info
struct, shared load extraction, typed read/write fault options, bounded key copy
and valid-tag output initialization changes. The private port still distinguishes
advertised length and actual transfer count; errors/fallback and accepted-state
semantics are unchanged. Test-option grouping prevents result/count swaps without
introducing a public API or backend.

Independently reran strict-warning Linux ASan/UBSan builds after these corrections:
conformance **163 checks PASS**, actual-core controller scenario **PASS**, and
independent public-only C17/C++17 consumers **compile/link/run PASS**. No sanitizer
diagnostics. The native fixture/standard CMake/CI/static-analysis final results
remain separately recorded by root; this review does not claim to rerun them.
