# Stage 1 Phase B plan, revision 6 (second reduced run, after the host-address gate PR #338)

Status: PLAN, not yet independently re-checked. Written 2026-11-08T11:11Z (GMT, system clock). Base: revisions 2 to 5 (r2, r3, r4a, r4b, r5, all with independent PASS) and the evidence record of the first run (PR #337, `docs/evidence/networking-physical-stage1/README.md`). Everything not named here is as in revision 5.

## 1. What happened since revision 5 (record)

- The run of revision 5 (R0' restore, B2' identify, B3 program, B3b carrier check, B4 capture, B5 restore) ended as follows: the board's Ethernet link came up and phase 1 completed, but the phase 2 host peer crashed on `bind` (`Errno 49 Can't assign requested address`) because macOS had removed the manual host address of `en13` during the link flap after the reset, most likely not yet restored at the moment of the bind (not captured). B5 restored the accepted normal image (written, `Verify successful.` twice, started, boot capture replayed through the accepted oracles). The board has run the normal image since.
- Fix #338 (merged as `b44b66a60bfc74c287fe62d874052e1d0868347c`): the peer starts only after read-only inventories show the dedicated NIC active with the static address (bounded 20 s; timeout is an inconclusive abort, no retry, no reset; the pending form is accepted only by the gate probe). Independent PASS at head `d255336`, 260 M5 and 140 M3 tests; CI green for `host`, `psa-crypto`, `links`, `zephyr controller` and `zephyr storage`; `zephyr io` and `zephyr runtime` red only at the SDK download step because of a GitHub release 504 (owner exception "너의 추천대로 진행해", PR comment posted by the main session, failed jobs re-run after the merge).
- `origin/main` also carries a docs PR of another session before #338; the hashed trees `tests/` (`ec9cd05b6fa1`) and `tests/target` (`fa60cc00e127`) and `docs/networking-physical-preparation.md` of main are identical to the reviewed `d255336`; the files that differ are PSA evidence documents, not hashed.
- Guard-2 expired at about 10:41Z; a second `DENIED` of a DAPLink mount attempt at about 10:29Z was vetoed. The owner re-arms guard-3 (`session-3.json`, `guard-3.log`, lifetime 14400) before B2' (see section 3).

## 2. Exact source and artefacts (replace section 2 of revision 5)

| Item | Value |
| --- | --- |
| Source | `b44b66a60bfc74c287fe62d874052e1d0868347c`; checkout `.worktrees/stage1-ws/crux-pilot-4` (new, detached, `git status --porcelain --untracked-files=all`: 0 lines); `crux-pilot` (f720c95, holds the guard script blob) and `crux-pilot-2`, `-3` stay, void |
| M5 manifest | `crux-pilot-4/build/evidence/m5-preparation/frozen-b44b66a60bfc/image-manifest.json`, SHA-256 `de76001549f7645330bc82bb4a20e6165e016a9537be19507cd3156adf955b95`; `prepare_m5_network.py verify`: success; 54 artefacts, 31 tools, dependency pins as in revision 3 (zephyr `dccb0959`, cmsis `512cc7e8`, cmsis_6 `30a859f4`, hal_nxp `7554bc0f`; littlefs `8f5ca347`, fatfs `f4ead3bf` recorded) |
| M5 BIN | 97,544 bytes, SHA-256 `aecee3541707f14218a18acb4135b5bfb7e04763f85e946bbed071fac66c6c3a`; compared with the `3ef758d` build it differs in exactly the 12 bytes of the commit tag (`cmp -l` bytes 94136 to 94147, 1-based); the tag `b44b66a60bfc` occurs once |
| M5 ELF / configuration | 2,255,292 bytes `92575495b89aa1eedfadc955e751366fedf8498a90fd08e0f8c61ebfa2f43bec` / 43,797 bytes `3afe989679ebc359a998792103dd0f011edc0d9b82b782cc134d8eb40b8ea4b7` |
| Plan | `.worktrees/stage1-ws/plan-m5-stage1-b44b66a.json` (file SHA-256 `025b7527c0f46632ed86678501ab5e2992eb74b7ec567903b46730df41ee19d2`); `plan_sha256` `f11aa7b22019f6a09a9579376253281109c2168eb61ae2906d3ead423078798c`; runner `2dcc7a37…5670` (unchanged), adapter `e8c446b4404b7fff0a0d139c4f18a4f8942fe200a347f8205944e8fd3595d809` (changed by #338); one new nonce `594231449` (`secrets.randbelow(0xfffffffe)+1`); same nine operations, reduced through phase 6, non-qualifying, no cable operations |
| Approval | `approval-m5-stage1-b44b66a.json` (file SHA-256 `1093a742a4971e96f62e5d2e6840e9a0c05c6c2e88f808b886f1ca362982725c`), the template with all nine flags true; `check-approval`: complete; **used only after this plan's independent PASS** |
| Void | plans and approvals of `f720c95`, `1b3ea3a`, `3ef758d` |
| Unchanged | entry and recovery image (`ad729684…1e02f`, manifest `07fce1ea…290c`), J-Link tools, pyserial venv, bench identities, guard script (SHA-256 `0baad879…616c`) |

## 3. Operations (commands from `crux-pilot-4/tests/target`, venv Python, `LC_ALL=C`, `TZ`/`COLUMNS` unset, the plan, approval and manifests of section 2)

All evidence under the new parent `.worktrees/stage1-ws/stage1-run-5/` (exists, empty; direct children `identify`, `program`, `boot-1`, `restore`). Guard session: **`/var/tmp/crux-guard/session-3.json`** (guard-3, started by the owner with sudo from the byte-identical guard script; accepted only after its log shows `ARMED`, a `DENIED exact_DAPLink_USB_serial=… disk=…` and `READY`, the guard process is alive and no RT1060 volume exists).

| # | Operation | Notes |
| --- | --- | --- |
| G3 | owner re-arms the guard (unplug J1, sudo block, replug at `ARMED`) | the board is unpowered while J1 is unplugged and boots the normal image after the replug; read-only checks by me only |
| B1 | read-only host inventory | `ifconfig en13`, `networksetup -getinfo "AX88179 3"`, `ls /dev/cu.usb*`, guard log and `ps`, disk, no J-Link name in any command line |
| R0 | not repeated | the restore path ran end to end on 2026-11-08 (R0', B5); B2' below shows whether the flash is intact after the power cycle of G3 |
| B2' | `identify` of the normal image, evidence `stage1-run-5/identify` | read-only; expected to match; a mismatch stops the run and is reported (no write) |
| B3 | `program` of the M5 image | as revision 3 |
| B3b | carrier check (`ifconfig en13`, up to 15 s, direct route via `en13`) | host only |
| B4 | `capture --boot 1 --through-phase 6` | with the host-address gate of #338: the phase 2 to 6 peers start only after the dedicated NIC shows `status: active` and `inet 192.168.77.1` (evidence `boot-1/peer-gate.jsonl`, `peer-gate/`); a gate timeout is `ABORT_INCONCLUSIVE` (no retry, no reset) |
| B5 | final `restore` | as revision 5 |
| B6, B7 | offline replays of the capture and the restore, end state | as revision 5; a new Draft PR with the evidence of this second run |

## 4. Failure handling and unknowns

- After a failure of B3, B4 or B3b: B5 as the only follow-up (revision 2 section 7), in `stage1-run-5` (its restore marker unused until then). After a failure of B2': stop and report, nothing written.
- All stop conditions of revisions 2 to 5 apply (any unknown or ambiguous state stops all hardware changes; no retry beyond the plan; the J-Link-name rule; disk floor 5 GB; the guard READY and alive before each step).
- Open unknowns: the real boot of the M5 phases 2 to 6 on this bench; the first packets after the link returns (ARP) are untested; the board's `READY` timeout is 90 s after each `READY` (`tests/networking_physical/main.c:38,67`), which the 20 s gate fits; the window between the gate's observation and the peer's bind still ends in `Peer failed during capture` / `ABORT_INCONCLUSIVE` if the address disappears again; the acceptance rule for the disconnect diagnostic of PR #336 has never been exercised on the board.

## 5. Timing and authorisation

Authorisation unchanged: the owner's approval of stage 1 board operations and the standing approval "pass하면 … 내 승인 기다리지 말고 해줘" (relayed), the overwrite "예" is not needed again (no overwrite of unknown content; the flash holds the accepted normal image). Expected: plan re-check about 11:25Z, B2' about 11:30Z, whole board window about 11:30 to 12:00Z (guard-3 lifetime 4 h).

## 6. Independent check (Opus, read-only, 2026-11-08, handed back about 11:14Z): PASS, no blocking finding; incorporated notes
- Recomputed and found true: trees equal to the reviewed `d255336` (differences only PSA documents), manifest verify, BIN differing from the `3ef758d` build only in the 12-byte tag, plan byte-identical regeneration, six tampered approvals refused, 260 M5 and 140 M3 tests pass, the first-run facts against the evidence files, the #338 CI facts, guard-3 READY.
- **Guard-3 record check (the code does not bind the executable):** accepted by the operator, done at 11:13Z: `session-3.json` has `sha256` `0baad879…616c` and the executable file hashes to it (git blob `992b40a8`, clean frozen checkout), mode `arm`, evidence `armed_replug_veto`, the current boot session UUID, uart `/dev/cu.usbmodem11402`, serial `0229…9905`, the `ps` identity of pid 55919; `/Volumes` has no RT1060 volume.
- **Do not touch `crux-pilot-3` until guard-3 ends:** guard-3 runs from `crux-pilot-3/tests/target/m5_mount_guard.py` and every guard check re-hashes it.
- The re-run of the CI jobs `zephyr io` and `zephyr runtime` after the merge was still pending when this plan was written.
- Open unknown added: a second `DENIED` DAPLink mount attempt at about 10:29Z (74 minutes after B5, nobody operating) shows the DAPLink re-enumerated for an unknown reason; between guard-2's expiry (10:41Z) and guard-3's READY (11:13Z) the DAPLink was powered with no guard running (no volume was found mounted at 11:13Z).
