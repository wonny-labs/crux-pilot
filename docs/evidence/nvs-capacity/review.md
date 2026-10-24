# Independent review record

## Initial review (before the outer-result assertion follow-up)

Scope: candidate 64 KiB BOOL/U32 capacity, CRC and backend-remount investigation
on baseline `ca7655b9ebce6debe444456eb0c4197a0f76f838`. Separate Reviewer and
Challenger agents inspected actual requirements, sources, generated configuration,
Twister JSON, retained handler logs and manifests. Neither authored this change.

**Reviewer — PASS, final retained evidence cross-check complete.** No material
findings. Independently matched all eight fixture and nine upstream source hashes;
per-profile config/Devicetree/ELF/executable/log hashes and observations; original
compiled native source entries; whole Flash image hashes and exact 64 KiB crops.
Verified both profiles and all six executed cases passed. Reviewed geometry,
registered-backend reset, acknowledged-only map, bounded keys, name/count overhead
and the nine-byte-name workload caveat against the pinned implementation.

**Challenger — PASS for bounded software investigation only.** No blocking
findings. Independently verified the same retained source/build/image identities,
actual Twister cases and observation tables. Also independently compiled and ran
the strict C17 golden-vector test. Checked full-read CRC/callback failure versus
outer Settings success, copied unvalidated bytes, partial-read limits and
nontransactional failed-save effects.

Both agree the evidence supports first-ENOSPC counts of 1939/1545, canonical
payload bytes 12602/10041 and post-reclamation/remount counts 986/789 (CRC off/on).
No code changes or full workload replay were needed for review. Author and both
reviewers matched the retained outputs to the unchanged inputs and successful run.

Limits: simulator only; final images follow the last mixed-roundtrip case, not
every intermediate phase. Backend state reset is not physical/process reboot.
No target timing/endurance, production capacity promise, Settings error-path
closure, native provider, patch/layout/backend adoption, hardware operation,
merge or issue-closure approval is granted. #14/#15/#16 and M4 remain gated.

## Affected follow-up review — explicit outer-result assertion

**Reviewer — PASS. Challenger — PASS. No material findings.** Both independently
reviewed the change from `5239f8f508e98ab453a4480eb0e905b6791a2dc0`, including
the new assertion and documentation, and the newly executed manual gate. Neither
authored the change or replayed the workload. Tested `main.c` SHA-256:
`836533934229588a6e9655e633d61672dd627fdbe4d80e5eb701c4caa9680481`.

The full CRC-off/on rerun passed 2/2 configurations and 6/6 executed cases with
no warnings (636.42 seconds, emulated host elapsed time, not target timing).
Both reviewers matched all eight current fixture and nine pinned upstream hashes,
the fresh Twister report hash, generated configuration/Devicetree/ELF/executable
and handler logs, original native compile entries, all observations, whole Flash
image hashes and exact 64 KiB retained crops. Current `results.json` and logs
describe this rerun; the initial artifacts remain historical in Git history.

Before and after remount, CRC-on full native and callback reads return `-EIO`
while outer Settings returns `0`. The assertion explicitly characterizes that
pinned-baseline limitation; it does not permit publishing copied, unvalidated
bytes or accepting a hidden failure as a successful read or confirmed absence.

The author reran strict C17 golden vectors, dependency/native-free guards and
formatting; all passed. The host formatter was unavailable and the container's
repository wrapper could not resolve the host-absolute linked-worktree Git path,
so all tracked owned C/C++ files were checked directly with the same
`clang-format --dry-run --Werror` operation using version 18.1.8. Whitespace
checks passed. Standard Host/Zephyr CI remains separate from the manual gate.

All initial scope limits remain. CRC-enabled NVS is the preferred candidate;
the first-ENOSPC observation is not a production capacity guarantee. No native
provider, patch/layout adoption, hardware, merge or issue-closure approval follows.
