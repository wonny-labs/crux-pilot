# Independent software review

The Reviewer did not author these changes. Separate task reviews checked the
approved profile, actual source/tests, retained outputs and selected linked ARM
image rather than accepting the implementers' verdicts. These are technical
assessments; they grant no production, hardware, merge or closure authority.

| Slice | Reviewer result |
| --- | --- |
| Four-owner candidate | Spec PASS / quality PASS: request limits, resolved sample DTS, regression and compile evidence |
| Pre-C snapshot | Spec PASS / quality PASS: exact reset call ordering, stackless code, NOLOAD reservation, MMIO widths, refusal and SystemInit policy |
| Combined maintenance | Spec PASS / quality PASS after lower-media fix: File/Config lifecycle, actual GC/remount and consuming close effects |
| Read-only RAM context | Spec PASS / quality PASS: exact code/vectors/stacks, restoration/veneers, pre-clock dependency guard and actual native entry refusals |

The material cleanup finding was addressed in a scoped re-review: real LittleFS
close can sync dirty state after uncertainty. A private lower File-area fence
now denies write/erase/flatten before forwarding while allowing close to consume
native resources. Actual dirty-close RED forwarded writes; final nine-case
GREEN forwards none after refusal. No rollback or automatic recovery is inferred.

The final selected ARM image reviewed has ELF SHA-256
`0ce1946afc15a44c637708ade6a5c9d28b5ece60a5780b2f8f4b2db515ad4ba2`.
The final formatting rebuild preserved the complete BIN bytes; ELF debug identity changed.
All 255 temporary vector handlers target the internal terminal; context text is
ITCM `[0x2470,0x2548)`, vectors DTCM `[0x20000800,0x20000c00)` and the two 1-KiB
stacks occupy `[0x20000c00,0x20001400)`. No Cortex-M instruction/exception was
executed on hardware or in emulation. A controller-idle observation is not NOR
ready, and the candidate always refuses native Flash operations.

Unfinished mutating-path/deadline/startup/target-binding software and physical
state/qualification gates remain explicit. **NOT READY for physical execution or
write. M4 is incomplete.** The separate whole-change Challenger returned **PASS for the bounded Draft PR**,
with no new blocking finding. It confirmed all 31 input hashes, exact ELF/BIN
identities and 32 focused checks (8 geometry/request/image, 15 copied driver,
6 island and 3 snapshot). The private provisioning/boot controls are trusted
fixture authority, not enforcement against arbitrary native clients. Both
review roles retain the unfinished software, integration and physical gates.
