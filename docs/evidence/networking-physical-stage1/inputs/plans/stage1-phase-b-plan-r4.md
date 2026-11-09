# Stage 1 Phase B plan, revision 4 (delta over revision 3, after B2 found unknown firmware)

Status: PLAN revision 4a after the first independent check of revision 4 (REVISE: two blocking findings, addressed in sections 3 and 4; non-blocking notes in section 6); **needs the owner's explicit decision (section 3) before R0**. Written 2026-11-08 (GMT). Base: revision 3 (`stage1-phase-b-plan-r3.md`, independent PASS) and revision 2. Everything not named here is as in revision 3.

## 1. What B2 found (2026-11-08T08:01Z, `stage1-run-2/identify`, read-only)

- Identity probe matched: CPUID `0x411FC271` (Cortex-M7 r1p1), DPIDR `0x0BD11477`, probe S/N 602003106, VTref 3.322 V. The fixed lsof ownership checks passed on real hardware: `01-identity-only-owners-cu.txt` = exit 0, `p31421`, `f5`; `...-owners-tty.txt` = exit 1, nothing.
- `verifybin` of the integrated normal image (`ad729684…1e02f`, 107,872 bytes) at `0x60000000`: `Verify failed @ address 0x60001024. Expected 60 read 40` and Commander exited non-zero (so the wrapper reported "Probe exited unsuccessfully"). The flash does **not** hold the integrated normal image; what it holds is unknown. The driver stopped as designed (plan B2b). No flash write, reset or power action took place. The core may have been left halted by the connect.
- Resolved unknown: a verify mismatch makes Commander exit non-zero. Still unknown: `IsHalted` text (never reached), active-adapter `ifconfig`/`route`.
- Record: `stage1-run-2/identify/` (including `failure.json`, `01-identity-only.log`, `02-candidate-integrated-normal.log`) and `stage1-run-2/identify.console`; kept as evidence, not reused (mode markers consumed).

## 2. Change: R0 restore of the accepted normal image first, then the plan of revision 3 in a fresh evidence parent

The `restore` mode of the reviewed driver has no entry-image check (it was designed so that it can run from an unknown or failed state): `q` identity probe, `loadfile` + `verifybin` of the exact normal BIN, a second read-only full `verifybin`, then one `r` + `g` with a passive 15 s UART capture open (`restore_identified_normal_image`, `verify_normal_image`, `normal_acceptance_capture`; all nine approval flags are already true). It is used here once more, at the start.

| # | Operation | Command / action | Physical effect | Notes |
| --- | --- | --- | --- | --- |
| R0 | Restore the accepted normal image over the unknown firmware | `program_m5_image.py restore ... --evidence RUN0/restore` with `RUN0=.worktrees/stage1-ws/stage1-run-r0/` (new, empty directory created first) and the same plan, approval, manifests and guard session as revision 3 | writes the QSPI image region (an image of 107,872 bytes; J-Link rewrites whole 64 KB blocks, so **`0x60000000` to `0x6001FFFF`** is overwritten; about 5 to 10 s), verifies it twice, one reset and start | Needs the owner's decision (section 3). Success criteria: both `verifybin` print `Verify successful.`; `normal-capture` shows `*** Booting Zephyr` and `M3_BOOT source=ec94fbe… profile=integrated commands=0 fault_default=off`. After R0 the board is in the accepted normal state, which is what revision 3's entry gate expects |
| B2' | Identify again | `identify` as in revision 3 but `--evidence RUN/identify` with **`RUN=.worktrees/stage1-ws/stage1-run-3/`** (new, empty, created first) | none (read-only; the core may be left halted) | Now expected to match (`Verify successful`); if it does not, stop and report (no further change) |
| B3 to B7 | unchanged from revision 3 | `program`, B3b carrier check, `capture --boot 1 --through-phase 6`, final `restore`, offline checks, end state | as in revision 3 | all with `RUN` = `stage1-run-3`; the final `restore` therefore uses evidence parent `stage1-run-3` (its marker is not yet consumed there) |

Evidence parents: `stage1-run` (first stopped attempt, kept), `stage1-run-2` (B2 found unknown firmware, kept), `stage1-run-r0` (R0), `stage1-run-3` (identify, program, boot-1, restore). Each mode is consumed once per parent, so no marker is reused. All are direct children of `.worktrees/stage1-ws/`; evidence directories are direct children of their parent.

## 3. Owner decision required (relayed through the main session)

The run needs a yes to: **overwrite the unknown firmware in the QSPI flash (`0x60000000` to `0x6001FFFF`) with the accepted normal image (R0)**.

- **Backup:** the reviewed driver has no read-back command, so R0 as planned takes none. A read-back is technically easy on this bench (a `savebin` of 128 KB took about 5 s at 400 kHz with the same probe and J-Link version in an earlier session; B2 itself read all 107,872 bytes of the flash in about 3 s), but it would be an extra J-Link script outside the reviewed driver and would need its own plan step and review (about 15 minutes more). I do not propose it.
- **Likely content (an offline lead, not proven):** the byte that differs (`0x60001024`) is the low byte of the image-size field in the boot header. The PSA conformance test image of 2026-11-07 (120,384 bytes, size field `0x1D640`, SHA-256 `31e3b8f3…`) is identical to the normal image in bytes 0 to `0x1023` and has `0x40` at `0x1024`; its BIN and a 128 KB read-back of it are kept on the host, and its evidence record says the previous board image was not restored afterwards. So the flash probably holds that PSA test image, which can be rebuilt or reflashed from the kept BIN. Nothing proves that no write happened since.
- **Recommendation:** accept the overwrite without a new backup, because the probable content is already preserved on the host and the normal image is the accepted state the plan ends in anyway.

The existing authorisation covers "all board operations of stage 1 ... programming the exact image" and the owner's wish to upload firmware, but plan r2 stated that programming over unknown firmware needs a new reviewed plan or an explicit owner decision, so this plan asks.

## 4. Unchanged limits and stop conditions

All stop conditions of revision 2 section 6 and revision 3 section 3 apply, including: any mismatch or unexpected text stops all hardware changes (no retry); the guard must be READY and alive before each step (deadline about 10:41Z, each guard check needs 90 s of lifetime left); no command line of any session may contain a `JLink`, `openocd` or `pyocd` name while a step runs; free disk above 5 GB. **After any failure of R0, nothing more is done: stop and report** (no second `restore`; a second one would also have no unused evidence parent). Which state that leaves depends on where it stopped: a failure before or inside `load-normal` leaves the flash unknown; a failure after both verifications passed (for example a missing boot banner) leaves the twice-verified normal bytes in flash. The report says which. This is narrower than revision 2 section 7, which allows one follow-up `restore` only after a failed B3, B4 or B3b; that rule is unchanged for those steps (they use evidence parent `stage1-run-3`, whose `restore` marker is unused until B5).

## 5. What a pass would and would not prove

Unchanged from revision 3: one boot of the M5 image on this bench; not HTTPS, SSH, SFTP, entropy, persistence or timing. R0 additionally shows that the accepted normal image can be restored and boots from the unknown state (raw capture only; acceptance analysis is the offline replay of B6).

## 6. Notes from the first independent check (REVISE, non-blocking ones)

- B6 also replays R0's `normal-capture` with the accepted normal-image oracles (same replay as for B5), so R0's result is judged offline, not only by the driver's banner check.
- Timing: R0 adds about 1 to 2 minutes of probe steps plus the 15 s capture before `identify`; the plan still ends well before the guard deadline if B2' starts by about 09:30Z.
- The earlier guess that the flash still held the M4 microSD image is replaced by the PSA lead of section 3; r4 does not claim to know.
