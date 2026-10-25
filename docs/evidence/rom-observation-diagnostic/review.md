# Diagnostic software review

Independent task Reviewer: **PASS**, source `aebc737`, base `822f6c6`.

The reviewer inspected the approved diagnostic-only requirements, actual source
and generated native copies, exact-image disassembly/configuration, test logs,
and retained package hashes. This was source/evidence review; tests were not
independently rerun and no hardware was accessed.

The initial review required R1: owned RAM files inherited the native library's
warning options rather than the required owned-code policy. The scoped fix,
actual compile commands, expected failing regression, 16-test successful gate
and unchanged ELF/BIN were independently checked. No material finding remains.

The selected status/ID graph, data/buffers, stack/exception context, pre-C and
runtime separation, exact LUT restoration, initial WIP/WEL refusals and uncertain
RAM disposition were examined. Automated instruction checks alone are not a
general closure proof. Manual reinspection is required when the image changes.

The reviewer also checked the disposable main `b32b858` union: ten preserved
capability subtrees, inactive System provider, nine retained artifact hashes,
16 diagnostic plus 3 snapshot successes and branch-identical BIN. The initial
missing-environment failure was retained and was not reported as a success.

Physical-use blockers remain: fitted-part facts, loader footprint, preservation,
backup/recovery, debugger/boot/readout/reset effects, watchdog and exclusive
controller/GPT2 ownership. This review grants no hardware, adoption, merge,
closure or M4 completion authority.

Final independent Challenger: **PASS at `f83ff69`** after C1 correction.
The whole-change review identified one documentary omission: the SDK clears
`FLSHCR2[A1].CLRINSTRPTR` before each accepted read. The scoped re-review checked
its explicit disclosure in the sample, public assessment and retained hardware
packet, refreshed documentary identities and preservation of all 63 earlier
execution artifacts. No implementation/configuration/test change or rebuild was
needed. No material finding remains within the approved software scope.
