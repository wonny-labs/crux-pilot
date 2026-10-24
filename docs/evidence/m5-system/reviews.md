# Independent System implementation reviews

Reviewed source: `042086b177a7e91fac80bf39bec9bf4361735925`.
Baseline: `bf048c13aa039be8c8e7ef85fd1d7d38c7f9194c`.
Human-approved contract: `72e5295`.

Two separate agents who did not author the platform implementation independently
read actual sources, pinned native facilities, contracts, configuration/tests and
retained evidence. They did not rerun all suites or operate hardware. Neither
PASS grants merge, hardware or Issue-closure approval. Final hosted CI is an
external acceptance gate on the published HEAD.

## Reviewer — PASS for implementation and local software review

No unresolved material findings remain. Earlier REVISE findings were corrected:

1. Controller freshness had been1000/1000ms instead of approved500/16000ms. Source
   now preserves the existing policy, with independent Time501ms and network
   16000/16001ms feed-boundary assertions and actual RED/GREEN evidence.
2. System-specific portable source negatives, invalid provider selection and
   actual public-facade private-include leakage tests are present and passing.

The review confirmed exactly six portable operations; private native/context
locking; serialized one-owner activation; unexpected prearming and uncertain
installation quarantine without rollback/rearm; service reports without platform
application policy; raw cause consistency validation and nondestructive query;
one explicit destructive clear attempt with verified readback; and unlock before
native nonreturn reset dispatch. Known competing native owners/bindings are
rejected, and normal Host never resets the machine/process.

The cross-task reset assertion avoids the original recursive-mutex weakness.
All35 recorded input hashes match reviewed files. Inspected results include
Host/sanitizer25/25 each, consumer/sanitizer2/2 each, Twister2configurations/3cases,
ten unaffected native negatives plus corrected disabled-case rerun, and final
EVKB40848B Flash/7616B RAM. Retained failures are described honestly.

Four hosted Host/Zephyr PR/push results still have to satisfy the final published
HEAD gate. Physical timing/reset/latched evidence and production guarantees are
outside this review. Prior process-exit delays remain unresolved. Nonblocking
trailing whitespace in retained logs was normalized in the review-record commit;
this does not change source/configuration or input hashes.

## Challenger — original final report

# Independent Challenger — PASS

Reviewed implementation commit `042086b177a7e91fac80bf39bec9bf4361735925` against
baseline `bf048c13aa039be8c8e7ef85fd1d7d38c7f9194c` and the six-operation
contract human-approved at `72e5295`. I did not author or modify implementation,
push/merge, or operate hardware. This is an independent software design/source/
evidence review, not physical qualification or permission to merge/close #30.

## Decision

**PASS for the implementation review gate.** The narrow mechanisms and explicit
provider limitations match the approved Pilot scope. No material unresolved
implementation finding remains. Hosted CI on the final published HEAD and the
separately approved physical acceptance remain separate gates.

## Challenges resolved

1. `samples/controller/system/controller_system.c:11–12` originally initialized
   both freshness limits to 1000 ms. This conflicted with the adopted Time
   500 ms/network 16000 ms scenario and weakened the Time feed condition. The
   implementation now uses 500/16000; `tests/system_consumer/host/main.c:15`
   onward checks those values plus 501 ms and 16000/16001 ms feed boundaries.
   Retained `sample-policy-red.txt` and actual Host/consumer GREEN evidence
   substantiate correction. **Resolved.**
2. The original reset hook probed the metadata mutex from the same reset thread.
   Zephyr mutex recursion meant that was not proof of release. In
   `tests/system/zephyr/src/main.c:90` onward, the reset child now signals and
   waits inside a truly nonreturning hook; the parent performs a public status
   observation before release/join (`:211` onward). Final Twister evidence
   covers this corrected test. Production common code unlocks before dispatch
   (`components/system/src/system.c:130`). **Resolved.**
3. The disabled-resource negative initially failed on absent `fsl_wdog.h` before
   its intended diagnostic. `components/system/ports/zephyr/system_binding.c:14`
   now checks alias existence/enabled state before HAL inclusion. The selected
   disabled case passed its affected rerun; ten unaffected composition cases
   had matched intended failures. Final positive EVKB link also passed.
   **Resolved.**

## Independent assessment

- Common lifecycle serializes admission, issue/feed state and observation. A
  prearmed binding faults without ownership; installation/setup uncertainty
  quarantines without disable/rollback/retry. One successful owner can delegate
  its boot-local token; tokens do not arbitrate unknown native clients.
- Native CMake rejects the real `CRUX_CONTROLLER_M3` selection even when command
  options are off, plus WDT/HWINFO shell and Task-WDT hardware fallback. The
  existing integrated M3 selection depends on that same owner symbol. This
  addresses native arm/feed and destructive boot-cause clear coexistence without
  introducing a reservation framework.
- I independently inspected pinned Zephyr `684c9e8f...` and NXP `2c2f28ac...`:
  the selected MCUX driver is `wdog01`; its running observation reads WDE and
  refresh protects its key sequence. The exact 500..64000 ms/500 ms subset
  encodes 0..127 in the actual Zephyr timeout calculation. No universal range,
  rollback, measured timing, or successful hardware-feed acknowledgement is
  implied.
- Reset never returns OK, normal Linux Host rejects it without process/machine
  effects, and Zephyr dispatch occurs outside the component mutex through a
  nonreturning native call. A hung native reboot remains explicitly distinct
  from a proven physical reset.
- Raw native cause/support consistency is checked before normalization; named
  combinations survive and other advertised native categories map to OTHER.
  Status probes do not authorize clear. Explicit successful public get grants
  one admitted clear attempt, consumed on failure; success requires zero flags
  on readback. Application-owned retention/delivery and clear ownership remain
  declared composition responsibilities, not authentication or a journal.
- Task-context checks precede native locking/effects, and native mutexes protect
  component metadata. There is no spin loop, automatic feeder, new worker,
  generic registry or expansion beyond six operations. Public contracts and
  independent C17/C++17 runtime helpers remain native-free; scoped negative
  source and public-usage checks accompany actual consumers.

## Evidence and limitations

I inspected retained logs under `docs/evidence/m5-system/`, the validation record,
actual source/build/configuration changes, and native sources. I independently
verified every `input-sha256.txt` entry against the reviewed source: no mismatch.
I did not independently rerun the container suites or physical scenarios.

Retained focused results: Host 25/25, ASan/UBSan 25/25, independent actual Host/
policy 2/2 and sanitizer 2/2, native Twister 2 configurations/3 cases, intended
composition rejection (10 unaffected plus corrected disabled-case rerun), scoped
boundary checks and affected format/dependency/static analysis. Final six-public-
operation EVKB link reports Flash 40848 B/RAM 7616 B. These are whole-image/static
observations, not System-only cost or stack-headroom/WCET proof.

Full Host/Zephyr PR and push CI results must be checked on the final published
HEAD. At review time they are pending. Parent agreed to replace the validation
table's premature wording with an explicit external acceptance criterion:
require four SUCCESS results on final published HEAD and inspect PR checks for
current status. That docs-only correction does not alter this source review.

No physical watchdog expiry, reset propagation, fresh physical boot provenance,
latch persistence, debugger/low-power behavior, oscillator tolerance, endurance,
sustained-load or scheduling margin is qualified. The minimal physical proposal
is appropriately bounded and separately gated. Previously recorded process-exit
delays remain unresolved; these passing tests do not establish attribution or a
fix. This PASS grants no hardware, merge or Issue-closure authorization.
