# Settings error-propagation experiment

## Decision and scope

This is a software-only proposal for Issue #16, authorised by the user's
23 October 2026 M4 Final Software Acceptance and Settings Investigation Decision.
Crux Zephyr Native Integration owns the investigation and any separately approved
downstream maintenance. No native provider, upstream patch, production storage
layout or writable Flash configuration is adopted here. Issue #16 and M4 remain
incomplete. PRs #42 and #43 retain their software acceptance; merge authority is
still required, in order #42 then refreshed/retested #43.

Baseline: Crux `225cd21b5fff37c31c5dff52570e9f26179682a1`; Zephyr
`684c9e8f32e4373a21098559f748f06915f950c9` (4.4.0). The runner rejects another
revision or modified inputs, copies four source/header inputs into a temporary
directory, records SHA-256, and applies two patches only to those copies with
zero fuzz. Pinned inputs are checked again after testing. Patch files are
experimental evidence, not build inputs to Crux or Zephyr firmware.

## Verified paths and proposed corrections

| Path | Original finding | Candidate behavior and evidence |
| --- | --- | --- |
| Store subtree/direct traversal | Backend errors lost; later sources can mask earlier failure | Return first error, unlock, avoid commit after failed subtree traversal; direct positive callback stop remains valid |
| Single-value read | Advertised length can conceal failed/short actual read | Preserve negative callback result; require requested copied count; intentional small/zero capacity still returns full stored size |
| Length query / multiple sources | Negative optimized length and earlier scan errors can be overwritten | Signed result, stop on first negative; valid later-source override retained |
| NVS load and save scan | Hard read errors treated as missing/free slots | Only `-ENOENT` means missing; hard errors stop before repair/writes |
| NVS load repair | Delete/count-write failures ignored | Check both deletions and count write; publish repaired local count only after acknowledgement; no rollback implied |
| Stored name, cached and uncached | Full stored length used to index fixed buffer | Check bounds before terminator; reject zero/empty or embedded-NUL names with `-EBADMSG`; allow optional single trailing NUL |
| Init count record | Any negative means empty; short/long/out-of-range accepted | Only `-ENOENT` initializes empty; exact 2-byte count in `0x8000..0xbfff` required |
| Cache | Error return cannot fit private unsigned ID-only helper | Private signed result; successful hit, complete miss, overflow scan and stale entry controls retained |

The candidate preserves NVS write return zero (unchanged data), full positive
write lengths, missing-record repair, positive callback termination and normal
successful reads. An early positive callback stop does not mark the whole cache
loaded. These return-code changes affect best-effort/multi-source behavior and
need upstream/API review before adoption.

The name bounds defect already has an upstream correction:
[Zephyr PR #111314](https://github.com/zephyrproject-rtos/zephyr/pull/111314),
commit `e79a0db70fc9ad33982f9786b428b115a78eaadd`, skips oversized names.
Do not claim a new discovery or resubmit that fix. This candidate additionally
proposes a fail-closed malformed-record policy and error propagation. It does not
validate every Settings name grammar rule or establish hostile-input security.
The published upstream advisory is
[GHSA-q7c8-m2qg-385c](https://github.com/zephyrproject-rtos/zephyr/security/advisories/GHSA-q7c8-m2qg-385c).

## Reproduction and verification gate

Requirements: clean pinned Zephyr, Python 3, patch, Git, C17 compiler with
AddressSanitizer/UndefinedBehaviorSanitizer. From the repository root:

```sh
python3 tests/native/settings_error/test_experiment.py \
  --zephyr /path/to/zephyr --variant original --report /tmp/settings-original.json
python3 tests/native/settings_error/test_experiment.py \
  --zephyr /path/to/zephyr --variant candidate --report /tmp/settings-candidate.json
```

Original is an intentional RED negative control and must exit nonzero; it runs
malformed records in separate bounded processes, so sanitizer termination is
expected for unsafe original cases. Candidate must pass every test with no
sanitizer output. Every case has a literal expected outcome and checks actual
return/status, operation counts, cache/count state, bytes copied and lock balance
as applicable. A 30-second process timeout bounds compiler/test invocations.
The retained JSON records input and patch hashes, compiler flags and per-case
observations. Different Host errno numbers are expected and computed locally.

This experiment has a documented **manual PR verification gate**: rerun both
variants after any patch/harness or pinned-source change and obtain independent
review. These tests are not part of standard CI. Existing Host/Zephyr CI remains
regression coverage; its success does not imply this experiment ran. A new CI job
or infrastructure is unnecessary for this unadopted, pin-specific proposal.

## Explicit limits and remaining gates

The actual copied `settings_store.c` and `settings_nvs.c` are compiled; NVS API,
Flash, locks, list/logging/build facilities and handler routing are Host doubles.
This does not compile the actual NVS engine or full Zephyr Settings registration,
execute storage, establish XIP safety or prove reboot/power-loss recovery.

- Ordinary registered `h_set` errors are still suppressed in `settings.c`; this
  two-file proposal does not fix that path. Do not claim all Settings APIs safe.
- `save_start`/`save_end` lifecycle errors and all backend combinations are not
  qualified. The per-parameter `save_one` path is the relevant future consumer.
- Save-side local count publication before a failed count write remains unchanged;
  retry/restart consistency and multi-write ordering need a focused adoption
  assessment. Earlier deletes/writes, copied bytes or handler effects can remain
  after failure. Error visibility is not rollback or transactionality.
- No complete concurrency, cache coherence, corruption, name grammar, NVS garbage
  collection, capacity/endurance, CRC or real-media failure qualification.
- #14 medium/backend/layout approval, #15 provider synchronization (including
  spinlock across blocking I/O), Flash/XIP and image/erase protection gates, and
  actual Settings/NVS native integration remain outstanding. Physical validation
  needs its own explicit authority, target/data identity and recovery procedure.

## Proposed upstream and maintenance decision

Recommend preserving this bounded experiment as regression evidence, without
adopting its patches. First evaluate the existing upstream bounds fix/release for
the accepted pin. Submit separate minimal error-propagation/length and NVS repair
proposals with focused tests and explicit return-compatibility discussion, rather
than a private permanent fork. Upstream acceptance and backport availability are
unknown; neither is claimed by the passing experiment.

Before upstream submission, a human must own and review the contribution, provide
any DCO sign-off, and include required AI attribution under the current
[Zephyr contribution guidelines](https://docs.zephyrproject.org/latest/contribute/guidelines.html).
No upstream issue/PR is submitted by this task.

Any temporary downstream adoption requires separate human approval of exact
patches and pin, affected native consumer path, retry/partial-effect policy,
remaining handler/engine gaps, and maintenance ownership. Cost is two source-file
patches plus pin-specific shims/tests, rebase and semantic review on each Zephyr
update, native integration tests, and eventual patch removal when upstreamed.
No reliable schedule or numeric maintenance estimate is established. If that
cost or compatibility policy is rejected, stop the affected native integration
and reassess the approved storage path rather than weaken the Configuration
contract or silently treat errors as missing values.

## Retained results

The evidence directory contains per-case [original macOS](evidence/settings-error-experiment/original-macos.json),
[candidate macOS](evidence/settings-error-experiment/candidate-macos.json), and
[candidate Linux](evidence/settings-error-experiment/candidate-linux.json) results.
Candidate: **94/94 PASS** on both Hosts with strict C17 and ASan/UBSan.
Original: **50 expected failures / 94 cases** on macOS, including the known name
bounds sanitizer failures. These are mixed positive and negative cases, not 94
negative tests. All retained harness/patch hashes match final files and pinned
inputs remained unchanged. The [independent review record](evidence/settings-error-experiment/review.md)
contains separate Reviewer and Challenger PASS verdicts and their exact limits.

Author-run Linux regression: `scripts/check-format.sh`,
`scripts/check-dependencies.sh`, and `scripts/check-host.sh` passed, including
**76/76 Host tests** and the stdin/stdout sample scenario. The existing Linux
Dev Container image was used with the cached pinned tool versions and an isolated
source snapshot/build directory. See [regression log](evidence/settings-error-experiment/linux-regression.log).
No production-source or firmware configuration changed, so a new target build is
not evidence for the experimental patches; a full Zephyr/native integration build
and tests remain necessary before any separately approved patch adoption.
