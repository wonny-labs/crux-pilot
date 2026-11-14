# Stage 1 Phase B plan, revision 4b (one-line amendment over revision 4a)

Status: PLAN, needs its narrow independent check. Written 2026-11-08 (GMT). Authorised by the main session after the R0 stop (relayed): "(B) GO for the one read-only `identify` of the normal image in a new parent `stage1-run-r0b` ... it must not write, reset or start anything beyond what identify does, and may leave the core halted ... If the narrow check asks for more than that, skip B."

## What happened (record)

R0 (restore, `stage1-run-r0/restore`, 2026-11-08T08:21:25Z): the identity probe passed, `loadfile` of the accepted normal image wrote 0x60000000 to 0x6001FFFF in 7.0 s, `verifybin` in the same command printed `Verify successful.`, and after `q` Commander printed `****** Error: Could not start CPU core. (ErrorCode: -1)` with exit 0 (the diagnostic that docs/m2-physical-verification.md already documents). The M3 wrapper stopped on it; the driver stopped before the second `verifybin` and before the one `r`/`g` start. Per r4a §4 nothing more was done on the board and no second `restore` is allowed.

## The amendment

One extra operation, **B-r0b**, read-only: `program_m5_image.py identify` with the same plan, approval, manifests and guard session as revision 3, evidence `.worktrees/stage1-ws/stage1-run-r0b/identify` (new empty parent, created beforehand; its `identify` marker is unused), candidate = the integrated normal manifest `07fce1ea…290c` (the only candidate), run from `crux-pilot-2/tests/target` with the venv Python.
- Physical effect: J-Link connect and read-only `verifybin` + `IsHalted` of the normal BIN (a second verification of the flash after R0, and the real `IsHalted` text). No `loadfile`, no `r`, no `g`, no erase, no power or cable action. As in every identify run the connect may leave the core halted (it is already in an unknown state).
- Outcomes: match (`Verify successful.`) is recorded as evidence only. A mismatch, a probe diagnostic (including the same disconnect diagnostic after `q`: the frozen driver has no tolerance for it) or any unknown text stops the run, and I report it; nothing else is done.
- What it does not do: it does not satisfy R0's success criteria, does not start the board, and does not authorise B2', B3 or any later step of revision 4: those wait for the driver fix (PR #336), the re-freeze, a new plan and its re-check.
- Evidence parents: `stage1-run` (stopped identify), `stage1-run-2` (identify found unknown firmware), `stage1-run-r0` (restore stopped after one verify), `stage1-run-r0b` (this identify). The new parents of the later run are named in the next plan.
- Stop conditions, guard checks, the J-Link-name rule and the disk floor are unchanged.

## Narrow check (independent, Opus, read-only): PASS, no blocking finding
Non-blocking notes incorporated: (1) "no `r`, no `g`" is true of the command files, but Commander itself may try to start the core when it disconnects after `q` (the R0 message came from that attempt); it could therefore start the verified normal image or print the same message and stop; both are harmless and within "what identify does". (2) If the first identity-only `q` step already prints the diagnostic, the run stops before `verifybin` and records nothing beyond R0's evidence. (3) The approval document is reused in a new evidence parent (accepted since r3); the next plan states this explicitly.
