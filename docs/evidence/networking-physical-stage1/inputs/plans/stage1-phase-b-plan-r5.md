# Stage 1 Phase B plan, revision 5 (delta over revisions 3, 4a and 4b after PR #336)

Status: PLAN, not yet independently re-checked. Written 2026-11-08T09:12Z (GMT, system clock). Base: revision 3 (PASS), 4a (PASS), 4b (PASS, executed). Everything not named here is as in those revisions.

## 1. What happened since revision 4b (record)

- R0 (restore, `stage1-run-r0/restore`, 08:21Z): flash written (0x60000000 to 0x6001FFFF) with the accepted normal image, `Verify successful.`, then the driver stopped on the documented disconnect diagnostic `Could not start CPU core. (ErrorCode: -1)` printed after `q` (exit 0): no second verify, no start, no capture.
- B-r0b (read-only `identify`, `stage1-run-r0b/identify`, 08:27Z): the flash matches the normal image byte for byte (second verification; no diagnostic); real `IsHalted` text `CPU is halted (PC = 0x600156C2).` (PC inside the normal image). The board state since: normal image in flash verified twice, `en13` status active.
- PR #336 (driver accepts exactly that diagnostic after a verified `loadfile`/`verifybin`/`q` command file, never the `program` or start shapes; real transcripts of the bench as fixtures): independent REVISE, then PASS at head `76d2933`, CI green, squash-merged by the main session as **`3ef758d710d4ebdf974f69350ce8c509772a289e`**. `tests/` tree `ab352ffd…` and `tests/target` tree `44cde368…` of main are identical to `76d2933` (no file differs at all).

## 2. New exact source and artefacts (replace section 2 of revision 3)

| Item | Value |
| --- | --- |
| Source | `3ef758d710d4ebdf974f69350ce8c509772a289e`; checkout `.worktrees/stage1-ws/crux-pilot-3` (new; the `crux-pilot` checkout at `f720c95` still holds the guard-2 executable, identical blob; `crux-pilot-2` is void), `git status --porcelain --untracked-files=all`: 0 lines |
| M5 manifest | `crux-pilot-3/build/evidence/m5-preparation/frozen-3ef758d710d4/image-manifest.json`, SHA-256 `0c9a71cf981929ca2b688b27528029897c32e77edccbff7bd3ea4e3896601d06`; `prepare_m5_network.py verify`: success; 54 artefacts, 31 tools, dependency pins as in revision 3 |
| M5 BIN | 97,544 bytes, SHA-256 `33a1e1b80cfcd66e377437087ac5ff5a1a39204899a5dbd0957f548a235f2284`; compared with the builds of `1b3ea3a` and `f720c95` it differs in exactly the 12 bytes of the commit tag (`cmp -l`: offsets 94136 to 94147); the tag `3ef758d710d4` occurs once |
| M5 ELF / configuration | 2,255,292 bytes `17c75138ab398ba2951143c7a2e9ba2c15bd068f5dbb2140e389aad2eccc8a31` / 43,797 bytes `8921ef23dc3a443a7f6aa97e235d00f982ad7444951635c3757032266b06c523` |
| Plan | `.worktrees/stage1-ws/plan-m5-stage1-3ef758d.json` (file SHA-256 `bfdd1fb6…`); `plan_sha256` `1a012d8dc47a078ac258e3234680b11b72aae3ac6ed49f50463157575a7f278d`; runner `2dcc7a37…5670` and adapter `041575af…c0ce` unchanged; one new nonce `527584622` (`secrets.randbelow(0xfffffffe)+1`); same nine operations, reduced through phase 6, non-qualifying, no cable operations |
| Approval | `approval-m5-stage1-3ef758d.json` (file SHA-256 `52425b6d…`), the template with all nine flags true; `check-approval`: complete; **used only after this plan's independent PASS**; reused across the evidence parents below (as accepted since revision 3) |
| Void | `plan-m5-stage1-f720c95.json`, `plan-m5-stage1-1b3ea3a.json` and their approvals |
| Unchanged | entry and recovery image (`ad729684…1e02f`, manifest `07fce1ea…290c`), JLink tools, bench identities, guard-2 (READY, deadline about 10:41Z), the J-Link-name rule, disk floor 5 GB |

## 3. Operations (replace revision 4a's table and 4b; commands run from `crux-pilot-3/tests/target`, venv Python, `LC_ALL=C`, `TZ`/`COLUMNS` unset, plan/approval/manifests of section 2, `--guard-session /var/tmp/crux-guard/session-2.json`)

| # | Operation | Evidence parent | Notes |
| --- | --- | --- | --- |
| R0' | `restore` of the accepted normal image over the identical image already in flash | `stage1-run-r0c` (new, empty) | Purpose: run the fixed restore path (second verify, one start, 15 s boot capture) on the safe image **before** the M5 image is written, so that a defect of this path shows up while the board still holds the accepted normal image. Writes 0x60000000 to 0x6001FFFF; success: both `verifybin` print `Verify successful.` (an accepted disconnect diagnostic is recorded as `<step>.diagnostic-accepted.json`), `normal-capture` shows `*** Booting Zephyr` and `M3_BOOT source=ec94fbe… profile=integrated commands=0 fault_default=off` |
| B2' | `identify` of the normal image | `stage1-run-4` (new, empty; `stage1-run-3` unused and void) | read-only; expected to match |
| B3 | `program` M5 image | `stage1-run-4` | as revision 3 |
| B3b | carrier check (`ifconfig en13`, up to 15 s) | host only | as revision 3 |
| B4 | `capture --boot 1 --through-phase 6` | `stage1-run-4` (`boot-1`) | as revision 3 |
| B5 | final `restore` | `stage1-run-4` | as revision 3; the restore marker of this parent is unused until now |
| B6, B7 | offline replays (also of R0' `normal-capture`), end state | | as revision 3 |

The owner's yes (relayed 2026-11-08, "예", for overwriting the flash content with the accepted normal image) already covers R0 as a write of the same accepted image; no new kind of operation is added.

## 4. Failure handling

- After any failure of R0' nothing more is done: stop and report (the flash then holds the accepted normal image or an interrupted write of it: the report says which step was reached). No second `restore` in the same parent.
- B2' to B5 as in revision 3, including revision 2 section 7 (one follow-up `restore`, in the parent `stage1-run-4`, only after a failed B3, B4 or B3b, whose restore marker is unused until then).
- All stop conditions and unknowns of revisions 2 to 4 apply. Unknowns still open: the second `verifybin` step's behaviour with a halted core (the tolerance covers the documented message; r0b showed none for a read-only verify), the real active-adapter `ifconfig` and `route` (proxies only), and the real boot of the M5 image.

## 5. Timing

Guard-2 ends about 10:41Z. R0' about 09:35Z, whole window about 25 to 30 min, end about 10:05Z. If R0' cannot start by about 10:00Z the main session prepares guard-3.

## 6. Independent check (Opus, read-only, 2026-11-08, received before 09:12Z (R0 prime started 09:12:26Z)): PASS, no blocking finding; incorporated notes
- All recomputed facts match (trees equal to `76d2933`, manifest verify, BIN differs from earlier builds only in the 12-byte tag, plan regenerates byte-identical, six tampered approvals refused, 253 M5 + 140 M3 tests pass).
- **R0' may write nothing:** J-Link compares before writing and skips an identical flash ("Skipped. Contents already match", as in an earlier PSA run on this bench); R0' then does not write 0x60000000 to 0x6001FFFF and probably shows no disconnect diagnostic. Read R0' as "writes, or skips if identical"; the report states which happened. The acceptance rule's first real use on the board may then be B5 (it is covered by the real-log unit test; the driver still requires `Verify successful` either way).
- **Added unknown:** the capture lease's own `verifybin … q` preflight does not use the new tolerance: if the same diagnostic appears there, B4 stops (safe; the revision 2 section 7 follow-up restore applies).
- The board state "en13 active" is from 08:27Z; the B1 inventory before R0' refreshes it.
