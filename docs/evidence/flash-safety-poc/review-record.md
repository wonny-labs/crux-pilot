# Independent review provenance

This is an author-maintained summary of separate-context reviewer results, not a
cryptographic attestation, verbatim transcript or GitHub Approve submission.

## Local implementation review

Reviewer: Codex subagent `/root/flash_probe_review`, separate from implementation.
Final implemented source revision: `10b32a732c140c20f936b3b695296d0b098ddf5e`.
Final reviewed ELF SHA-256:
`eb313d4c8df82e9c45b32be4bfadb02f16e4437742928f612621e2aec575fc0c`.

Material findings and resolutions:

- Single-command tests could not prove stopping before remaining work. Added
  three-command write/block requests failing at command two and mixed erase
  failing at operation sixteen, with reset/poll/command/unlock assertions.
- Broad loop instrumentation also bounded healthy TX/RX data-progress loops.
  Excluded those loops and added a generator regression check.
- Test lookup embedded a personal workspace path. Required `ZEPHYR_BASE` instead.
- Initial target ELF left the terminal at Flash `0x60007132` despite its requested
  placement. Compiled separate `terminal.c` in the MEMC library searched by the
  relocation scanner; rebuilt and confirmed ITCM `0x227c`, exact nop/back-branch
  bytes and no outgoing call. Added negative placement tests.
- A macOS process-launch/scheduling observation exceeded the harness's external
  0.3-second limit, then passed immediately on rerun. Increased host containment
  to two seconds, preserving the candidate iteration budgets.

Final verdict: **PASS for the limited software PoC**. The reviewer independently
reran focused fault/placement tests, checked generated sources against current
transformations, read retained Linux regression/build logs, and verified the final
ELF identity, terminal bytes and RAM controller-configuration pointer. The reviewer
did not rerun the full Linux Host regression suite. Evidence is retained beside
this record; complete temporary build outputs remain local build artifacts.

## Published technical review

[Independent technical review on PR #35](https://github.com/wonny-labs/crux-pilot/pull/35#pullrequestreview-5619919823)
reviews exact head `10b32a7` and gives PASS for the compile-only experimental scope.
Its GitHub event is COMMENTED, not APPROVED. It requests a proportionate CI/manual
gate, complete exact-head CI results and preserved review provenance.

The follow-up adds the focused harness to the existing Zephyr job and documents
manual full-build/ELF verification and final-head CI checks. It preserves the
production-timeout, terminal-policy, startup, exception/vector/NMI, DMA/bus-master,
complete code/data closure and Flash-layout/programmer safety blockers. These are
not silently converted into PoC merge prerequisites requiring production work.

Human merge acceptance is separate. Issue #22 remains Open and PR #35 remains Draft
pending that decision; this record grants no hardware or production authority.

## Follow-up review

The same independent subagent reviewed the CI/manual-gate and provenance changes,
reran the focused harness, checked CI workspace/bootstrap paths and confirmed that
PoC implementation/sample/test/verifier/input-hash files remain unchanged from
`10b32a7`. Verdict: **PASS for this follow-up**, conditional on checking final-head
workflow outcomes before merge. This review adds no production or hardware authority.
