# Independent execution review

Reviewer: `/root/execution_challenger`, independent from implementation and from the
runtime consumer author `/root/runtime_consumer`. Scope: approved execution contract,
Host/Zephyr implementation, reusable-handle/lifetime safety, consumers, build boundary,
maintained tests and retained validation. Source reviewed: `b9df57dfefba53c938a3cddbb7f8b139b7986ab8`
against main `32405ad`.

Initial implementation verdict: **REVISE**.

1. Host semaphore polling exposed a private bookkeeping-lock collision as BUSY while
   tokens remained available. A deterministic real-lock regression reproduced the
   failure; the implementation now serializes the short bookkeeping access, and
   the regression checks consumption/token preservation.
2. Admitted-operation and concurrent-join guarantees lacked controlled tests.
   Private test-only linker wrappers now pause after common admission and verify
   copied-handle concurrent join rejection, busy destruction for admitted mutex and
   semaphore operations, and subsequent completion/cleanup of the original lifetime.

Follow-up source review accepted both corrections. Additional corrections removed
an unarmed hook's use of an invalid initial pthread identity, kept stack-request
validation provider-specific, validated priority classes without unsigned enum warnings,
and propagated Zephyr test wrappers from static `app` to the final image link.
Scoped analysis annotations cover linker-mandated names, deliberately invalid enum
test inputs and the intentional initial/maximum argument order; no blanket production
warning suppression was added.

Final independent verdict: **PASS for software PR preparation**. The reviewer inspected
actual final logs and matching input captures: Host Debug and ASan/UBSan 45/45 each,
standalone consumers 1/1 each, native_sim 14/14 across 2/2 configurations, both EVKB
cross-builds and boundary checks, 160 build safeguard cases, formatting, dependencies
and full configured analysis. No remaining actionable source finding was identified
within that scope. The actual resource figures and fixed stack allocation were checked.

The reviewer explicitly retained the unresolved Time process-exit timeout. Twenty
successful direct repeats and subsequent complete suites support proceeding with
this scoped PR but establish neither the cause nor a cure. Physical acceptance,
hosted CI, Issue #24 enforcement, merge authority and Issue #25 completion are separate
gates. This review grants no new authorization for any of them.

The proposal also received independent REVISE/PASS before human contract approval;
its rationale and approval record remain in the approved contract document. The
runtime fixture was authored independently against that contract without inspecting
provider implementation source.

## PR #36 poll/deadline review follow-up

The PR review found that a zero-time poll could pass its initial bounded metadata
admission, complete the native operation, and then block while reacquiring metadata
to finalize token, mutex-owner or join bookkeeping. The implementation now retains
metadata across the native try and bookkeeping for zero-time semaphore take, mutex
lock and thread join. `Execution.HostPollFinalize` traps any post-operation blocking
metadata-lock call across all three operations.

An independent read-only re-review found no actionable race, deadlock, lifetime or
contract issue. It confirmed the Host and Zephyr poll native calls are immediate try
operations, and that metadata-dependent paths cannot hold the native object lock while
waiting for metadata. Host Debug and ASan/UBSan pass 47/47 CTest cases each. The full
portable rerun passes both consumers (1/1 each), native_sim (15/15 cases across 2/2
configurations), and both EVKB cross-build/boundary checks. Formatting, dependency
checks, configured analysis and 160 build safeguard tests also pass. Fresh outputs are
in the `review-follow-up-*.txt` files alongside this record.

The portable script now runs west commands from the selected pinned workspace. This
keeps its module resolution stable when the Crux repository is nested under a separate
managed worktree.
