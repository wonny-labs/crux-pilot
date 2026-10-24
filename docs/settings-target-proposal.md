# Settings/NVS target patch proposal

Status: **experimental software candidate; NOT READY for target adoption or first write**.
Baseline Crux `b31f4f05c053c8141543db9ac1e9c3d2620b16eb` (accepted simulator
PR #56, unmerged); Zephyr `684c9e8f32e4373a21098559f748f06915f950c9`.
The 24 October 2026 user direction authorizes this software proposal and bounded
verification. It does not authorize production patches, partition adoption,
upstream modification/submission, hardware actions, merging or Issue closure.
Previous #44/#45/#56 fixtures and evidence remain unchanged.

## Exact proposed scope

All patches reside in [the isolated fixture](../tests/native/settings_target_candidate/).
They apply only to verified temporary copies; no normal build selects them.

| Source | Proposed correction | Deliberately unsupported paths |
| --- | --- | --- |
| `settings_store.c` | Return first nonzero backend result from `settings_load_subtree_direct`, stop traversing sources, always release lock; preserve positive callback stop | Registered `h_set`, subtree commits, `load_one`, length queries, lifecycle errors |
| `settings_nvs.c` | Bound and validate stored names; propagate hard reads; reject one-sided pairs; skip completely absent deletion holes without deletes/count repair; validate claimed pairs before save effects; publish local count only after acknowledged count write; reject missing/malformed count | Name cache enabled; automatic orphan/count recovery; full-store CRC/corruption validation |
| `nvs.c` | Preserve negative Flash comparison errors at all three early startup comparisons | General GC/recovery algorithm redesign or universal nondestructive mount |

Supported candidate composition is name cache **off**, NVS data CRC **on**, bad
memory reinitialization **off**, and direct per-key traversal plus
`settings_save_one`. Existing simulator-only Configuration composition still
has its simulator guard. This task does not remove that guard or adopt a target
provider. No consumer-specific 256-byte policy is embedded in generic backend
patches. The Configuration callback already stages the full bounded record,
checks actual read length and preserves its error before publishing bytes.

A completely missing name/value pair within the claimed range is a normal
possible result of successful deletion; it is skipped and never repaired on
read. A name-only or value-only pair is ambiguous and fails with `-EBADMSG`.
Save preflight visits the **claimed range**, not the whole ID capacity, before
replacement, insertion or deletion. It detects ownership/read errors even below
an early matching key. It adds one name and one value-presence read per claimed
slot, followed by the normal name lookup. This is an explicit latency/read-cost
trade-off, not an endurance/capacity qualification.

Init rejects count `-ENOENT` with `-EBADMSG`, including a freshly erased region.
It accepts only an exact two-byte value in `0x8000..0xbfff`. A valid zero-entry
count is `0x8000`. The missing-count policy prevents treating lost metadata as
an empty store; it intentionally requires a separately approved provisioning or
remediation procedure. It does **not** itself provide that procedure.

Healthy replacement/deletion, positive direct callback stop, NVS unchanged-data
zero return and positive full write return remain supported. Compatibility
changes are observable: missing counts and orphans now fail; direct traversal
stops at errors/positive stops instead of visiting later sources; reads no longer
repair Settings metadata. Earlier handler/copy effects and multi-write save
side effects can remain after failure. Count publication correction is not a
transaction or rollback. A failed insertion after count publication can leave
an orphan that a subsequent attempt rejects.

## Mount writes, repairs and erases

`settings_nvs_backend_init` calls `nvs_mount` **before** reading the count.
Consequently even a later missing-count rejection is not a read-only preflight.
The pinned engine has these startup mutation branches (source function
`nvs_startup`; `nvs_gc` has its own normal relocation writes/erase):

- All sectors appear closed: `CONFIG_NVS_INIT_BAD_MEMORY_REGION=y` flattens the
  complete selected region. The candidate requires it **off**, returning
  `-EDEADLK` instead. No automatic format is proposed.
- Nonempty following sector with a valid GC-done marker: erase that following
  sector.
- Nonempty following sector without a valid GC-done marker: erase the current
  write sector, reset write pointers and restart GC, which relocates records,
  writes a GC-done marker where possible, and erases the reclaimed sector.
- Current sector contains no valid ATE but programmed data: erase that sector.
- Successful startup of an empty write sector: append a GC-done ATE, including
  the normal fresh-erased mount path. This is a write even with no user save.

The third comparison guard (already corrected by accepted PR #56) matters: original single-read failure at that
comparison was interpreted as uncertain layout, returned mount success and
performed **one write and two erases**, destroying a retained `a5` marker. The
candidate consolidates #45’s first/second guards and #56’s third guard, stopping
at the read error with no write/erase. This confirms existing evidence rather
than claiming a new defect discovery.

The patches retain normal successful engine behavior, including validated GC.
The eight focused startup tests do not qualify every recovery classification.
For unknown/corrupted target media, do not run mount as an inspection tool.
A future first-write procedure must establish read-only raw image/identity and
provisioning/recovery policy before invoking mount. Options are separately
approved offline inspection and remediation, or a reviewed engine preflight
that proves a recoverable layout before permitting mutation. Neither option is
implemented or authorized for hardware here. No destructive retry, clear,
autoformat or recovery fallback is supplied.

## Verification and reproduction

Host checks compile the actual copied Settings store/backend sources, using
NVS API/locks/handler-routing doubles. Expectations check API result, writes,
deletes, local count and lock balance. Cases cover healthy paths, preserved
errors, orphan refusal, deletion holes, malformed names/count, write/delete
failures, partial insertion and preflight below a matching higher key.
Actual-engine `native_sim` checks use a private Flash proxy and bounded simulator
region, with bad-region reinitialization disabled and data CRC enabled. They
cover empty-mount marker writes, first/second/third/persistent read faults,
all-closed refusal, marker-write failure and successful write/remount/read.
The separate actual-provider composition selects the narrowed copies with the
existing simulator-only Crux provider and actual Settings/NVS/Flash stack. It
explicitly provisions only simulated media and tests BOOL/U32 round trips,
unprovisioned/missing/malformed count refusal, orphan refusal without repair,
hard reads and full-record CRC failure with active-value preservation.

```sh
python3 tests/native/settings_target_candidate/test_candidate.py \
  --zephyr /path/to/zephyr --variant original --report /tmp/settings-red.json
python3 tests/native/settings_target_candidate/test_candidate.py \
  --zephyr /path/to/zephyr --variant candidate --report /tmp/settings-green.json

# Inside the existing Linux development container:
export PATH=/opt/crux-cache/venv/bin:$PATH
export ZEPHYR_BASE=/ws/zephyr ZEPHYR_TOOLCHAIN_VARIANT=host
west build -b native_sim tests/native/settings_target_candidate/startup \
  -d /tmp/settings-target-candidate/startup-original -- \
  -DCRUX_NVS_STARTUP_CANDIDATE=OFF
/tmp/settings-target-candidate/startup-original/zephyr/zephyr.exe
west build -b native_sim tests/native/settings_target_candidate/startup \
  -d /tmp/settings-target-candidate/startup-candidate -- \
  -DCRUX_NVS_STARTUP_CANDIDATE=ON
/tmp/settings-target-candidate/startup-candidate/zephyr/zephyr.exe

west build -b native_sim tests/native/settings_target_candidate/provider \
  -d /tmp/settings-target-candidate/provider -- \
  -DCRUX_SETTINGS_TARGET_CANDIDATE=OFF
/tmp/settings-target-candidate/provider/zephyr/zephyr.exe
west build -d /tmp/settings-target-candidate/provider -- \
  -DCRUX_SETTINGS_TARGET_CANDIDATE=ON
/tmp/settings-target-candidate/provider/zephyr/zephyr.exe
```

Original variants are intentional negative controls and must fail. Malformed
original names trigger bounded separate ASan processes. Candidate requires strict
C17 `-Wall -Wextra -Wpedantic -Werror` plus ASan/UBSan on Host, and strict owned
application warnings on native_sim. There is no capacity sweep or hardware run.
Retained reports/logs are in [fixture evidence](../tests/native/settings_target_candidate/evidence/).

| Suite | Original negative control | Narrowed candidate |
| --- | --- | --- |
| macOS Host C17 + ASan/UBSan | 27 expected failures, 15 passing controls / 42 | 42/42 PASS |
| Linux Host C17 + ASan/UBSan | 27 expected failures, 15 passing controls / 42 | 42/42 PASS |
| Actual NVS startup native_sim | 4 expected failures, 4 passing controls / 8 | 8/8 PASS |
| Actual Configuration provider + Settings/NVS native_sim | 6 expected failures, 2 passing controls / 8 | 8/8 PASS |

Two original malformed-name processes terminate under ASan; candidate Host runs
have no sanitizer output. The actual-provider CRC case emits the expected NVS
CRC-error log. Initial native configuration without an explicit toolchain could
not find an SDK; the successful runs select the installed Host toolchain as
shown above. Scoped formatting uses the existing bundled `clang-format` (the
`clang-format-18` executable name was unavailable in this container).
No standard CI job is added; rerun both controls after candidate/harness/pin
changes and obtain independent review before considering adoption.

Pinned source SHA-256 identities:

| Input | SHA-256 |
| --- | --- |
| `settings_store.c` | `14ce61fa29aaca20ec3e69c6f401434f7f6d5a242d2911d0e931ccaf1f58f0e6` |
| `settings_nvs.c` | `ab6aa643eae9f5c5ca8099ddac89cc63aaa2fd43f775a8de5efcb11aad3c0244` |
| private Settings/NVS header | `fabc913fb783f2a866b21f507727a40dc39932e4cd2b91b38331c2018c6f1bde` |
| public Settings header | `4135bf0fe67f0002aeebabb2fb45e8d7a3d6daa5f65b9a9daefc98eadf8743cc` |
| `nvs.c` | `21ad6c753b5bc8370502799568426ec69648ea41a8b0b263df0f3eb840f6767b` |

## Maintenance, upstream and adoption gates

Proposed owner: **Crux Zephyr Native Integration**, subject to explicit human
acceptance; no named individual or standing maintenance commitment is invented.
Maintain three pin-specific source-copy patches and their focused fixtures,
review semantic/configuration changes at each Zephyr upgrade, and remove patches
when equivalent upstream behavior is adopted. The source-copy scripts and shim
headers are experiment maintenance too. No delivery estimate is established.

Submit independently reviewable upstream-compatible changes, if separately
authorized: direct traversal status/stop propagation, hard-error visibility and
bounded name validation, count publication, and the missing startup guard.
The prior investigation already records upstream name-bounds correction
[PR #111314](https://github.com/zephyrproject-rtos/zephyr/pull/111314); do not
resubmit it as a discovery. Fail-closed missing-count/orphan policy and save
preflight need explicit return/latency compatibility discussion and may remain
a downstream policy choice. Obtain human contribution ownership, current
upstream contribution-rule review and any required sign-off before submission.
No upstream acceptance or submission is claimed.

Material blockers remain:

1. A one-byte value-presence probe does not validate the full NVS data CRC.
   Full per-key direct read checks the requested record, but save preflight can
   miss an existing corrupt value and subsequently overwrite it. The current
   candidate therefore **does not satisfy universal fail-closed corrupted-store
   recovery**. A separate bounded composition preflight or justified engine
   support is required before writable target adoption.
2. A valid-looking but stale count, records beyond the claimed range, duplicate
   logical names, malformed ATE classifications and general GC recovery are not
   qualified. No mount-time nondestructive guarantee follows from these tests.
3. Broader real-engine Settings/backend coverage, retry/partial-effect policy,
   concurrency/coherence, physical Flash/XIP safety, storage layout/image fences
   and independent review are separate acceptance gates.
4. Provisioning cannot be inferred from `-ENOENT`. Human approval must choose the
   exact initialization/remediation procedure and evidence to permit writes;
   this proposal neither adopts one nor changes Configuration defaults to hide
   uncertainty.

Recommendation: retain this bounded software evidence and review the exact
patches. Keep target writes disabled until those blockers and the responsible
human's adoption/provisioning decisions are resolved. M4 and the relevant Issues
remain incomplete.

The subsequent [readiness-gate experiment](settings-readiness-gate.md) demonstrates
a private read-only mount and full claimed-record CRC preflight, including before
save. It addresses the selected mount-mutation and corrupt-value overwrite paths
without another upstream patch. It does not establish whole-media integrity or
adopt a production composition; the remaining target gates above still apply.
