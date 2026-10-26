# Issue #26 implementation plan

Historical implementation plan for Issue #26. Its signed-ms Monotonic surface
is superseded by the [typed Time contract](../time-contract.md) and
[Thread sleep contract](../thread-sync.md); original design steps remain below.

Authorisation: user attached implementation goal approves final messaging proposal,
initial qualification configuration and cooperative stop. Baseline main 225cd21.
The approved contract is retained in docs/messaging-contract.md; optional abort
is excluded. Implementation is autonomous; final Draft PR needs human review.

1. Establish current Linux Host baseline and retain exact command result.
2. Monotonic (components/monotonic, tests/monotonic): public signed-ms now/sleep-until;
   RED/GREEN read failure/overflow and absolute deadline predicates, then real Host
   and Zephyr providers, upward rounding/ISR tests. Separate from RTC.
3. Queue (components/queue, tests/queue): exact-size public interface and 4/16/64 pool.
   RED/GREEN FIFO/copy/full/empty/errors before implementation. Shared portable
   lifecycle with private native mechanisms: atomic admission refs through waits,
   destroy BUSY preserves handle, generation retirement. Poll retains one critical
   section through commit; finite metadata admission consumes original deadline.
   RED/GREEN barrier-based sender/receiver, reclamation and MPMC integrity cases.
4. Independent consumer (tests/messaging_consumer): separately authored C17/C++17
   public-target fixture, repeated sessions, semaphore notification and protected
   stop checks after every operation including continuous traffic; join then destroy.
   Portable controller phase scenario preserves old historical fixtures.
5. Integrate CMake, Zephyr Kconfig/module, scripts/test-messaging.sh and source/build
   boundary inventories. Zephyr native_sim conformance/ISR/tick cases and EVKB builds.
6. Run Debug/sanitizer, native_sim, consumers, Execution/Networking/native-interface
   regressions, format/analysis/build safeguards. Record sanitised commands/results,
   AC matrix and matched-content resource comparisons. No hardware qualification.
7. Fresh independent Reviewer and Challenger inspect actual source/tests/evidence;
   correct findings and re-review. Draft PR, verify final-HEAD hosted CI, report
   limitations and stop for human approval; never merge or close #26.

Review focus: lifetime lookup/destroy race; zero-time cleanup cannot wait;
metadata + payload waits share budget; failed receive leaves output unchanged;
continuous successful traffic cannot bypass cooperative stop; fake clock tests
must complement actual native waits; boundary facade must hide native links.
