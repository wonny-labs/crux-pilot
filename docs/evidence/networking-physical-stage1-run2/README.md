# Stage 1 Ethernet bring-up on the RT1060-EVKB: second reduced one-boot run of 2026-11-08

Second hardware window of the reduced, **non-qualifying** one-boot run of the M5 network test image, after the first run (`docs/evidence/networking-physical-stage1/`, PR #337) stopped on a host race at phase 2. This record keeps the raw evidence of the second window, the analysis of why it stopped in phase 1, and the restoration of the accepted normal image. It does not qualify HTTPS, SSH, SFTP, entropy, persistence or timing, it is not the three-boot M5 series, and it refers to Issue #28 without closing it.

**Result in one paragraph.** The board's Ethernet link came up again and phase 1 (idle baseline) ran with clean samples, but the runner rejected phase 1 itself (`Resource settling/idle sampling deadline failed`), a runner defect that depends on the arrival order of two rows and that the first run had passed by luck. The host-address gate added after the first run was never reached, and phases 2 to 6 (TCP/UDP echo, silent peer, early close) again did not run: **INCONCLUSIVE** for echo, timeout and close behaviour. The accepted normal image was restored (written, verified twice, started) and replayed offline through the accepted oracles.

## 1. Authorisation

As in the first record (the owner's approval of the stage 1 board operations and the standing approval to proceed after independent PASS, relayed by the main session; the overwrite "예" of the first window is not needed again: no unknown content was overwritten, `identify` matched the accepted normal image before programming). Additional relayed decisions of 2026-11-08: the owner re-armed guard-3 by sudo; for PR #338 the owner approved merging on its independent PASS with `zephyr io` and `zephyr runtime` red only because of a GitHub release download 504: "너의 추천대로 진행해" (the failed jobs were re-run after the merge).

## 2. Source, artefacts and plan

| Item | Value |
| --- | --- |
| Source | `b44b66a60bfc74c287fe62d874052e1d0868347c` (main after PR #338), frozen checkout clean; the hashed trees `tests/`, `tests/target` equal the reviewed PR head `d255336` (only PSA documents differ) |
| M5 manifest | SHA-256 `de76001549f7645330bc82bb4a20e6165e016a9537be19507cd3156adf955b95` (`inputs/image-manifest-m5-b44b66a.json`) |
| M5 BIN / ELF / configuration | 97,544 bytes `aecee3541707f14218a18acb4135b5bfb7e04763f85e946bbed071fac66c6c3a` / 2,255,292 bytes `92575495b89aa1eedfadc955e751366fedf8498a90fd08e0f8c61ebfa2f43bec` / 43,797 bytes `3afe989679ebc359a998792103dd0f011edc0d9b82b782cc134d8eb40b8ea4b7`; compared with the build of `3ef758d` the BIN differs only in the 12-byte commit tag (bytes 94136 to 94147 counted from 1) |
| Plan | `inputs/plan-m5-stage1-b44b66a.json`, `plan_sha256` `f11aa7b22019f6a09a9579376253281109c2168eb61ae2906d3ead423078798c`, runner `2dcc7a37a49fb263460122c9f84392fbc1f1708032a6634e5bff6a5120565670`, adapter `e8c446b4404b7fff0a0d139c4f18a4f8942fe200a347f8205944e8fd3595d809`, one nonce `594231449`, reduced through phase 6, non-qualifying; approval `inputs/approval-m5-stage1-b44b66a.json`; plan document `inputs/plans/stage1-phase-b-plan-r6.md` (independent PASS before the run) |
| Normal image, J-Link, bench identity | as in the first record (normal image `ad729684749181f0c26853828550187a2c3e60dbb28cf3ee2194a7984141e02f`, manifest `07fce1ea…290c`, J-Link V9.86a, probe 602003106) |
| Guard | arm mode, started by the owner with sudo from the byte-identical guard script (SHA-256 `0baad879…616c`), log `inputs/guard-3.log`, session record `inputs/session-3.json` |

## 3. Timeline (UTC, system clock of the host)

| Time | Event | Result | Evidence |
| --- | --- | --- | --- |
| 11:12:45 | owner re-arms the guard (J1 unplugged, sudo block); `ARMED`; the owner replugs J1 | `DENIED exact_DAPLink_USB_serial=… disk=disk4`, `READY` (deadline about 15:12) at 11:13:06; no power command by the agent | `inputs/guard-3.log` |
| 11:14:09 | B2' read-only `identify` of the normal image | matched (flash intact after the power cycle) | `01-second-run/identify/` |
| 11:14:28 | B3 `program` of the M5 image | written (131,072 bytes affected, 6.5 s), `Verify successful.`, reset, start | `01-second-run/program/` |
| 11:15:02 | B3b carrier check | `en13` active with `inet 192.168.77.1`, route via `en13` | `01-second-run/b3b-carrier-check.txt` |
| 11:15:02 | B4 reduced capture `--boot 1 --through-phase 6` | one reset; phase 1 idle samples clean; the runner aborted at the end of phase 1: `ABORT_INCONCLUSIVE`, `Resource settling/idle sampling deadline failed` (result written 11:15:18) | `01-second-run/boot-1/`, `boot-1-safety/` |
| 11:16:35 | B5 `restore` | normal image written (131,072 bytes, 7.0 s), `Verify successful.` twice, one start, 15 s capture shows the normal boot banner | `01-second-run/restore/` |
| about 11:18 | B6 offline checks | see section 5 | `01-second-run/b6-offline-checks.txt` |

Earlier the same day (10:29Z) the guard-2 log shows a second `DENIED` of a DAPLink mount attempt with nobody operating the board (see section 6); guard-2 itself expired at about 10:41Z.

## 4. What the second run showed and why the runner stopped

- Board UART (`01-second-run/boot-1/uart-rx.bin.raw`): `M5_BOOT source=b44b66a60bfc`, `ARM`, `READY seq=1`, `LINK seq=0 at=1816 link=1`, `LINK seq=1 at=3827 link=2 address=1 ipv4=3232255234` (192.168.77.2), three `IDLE checkpoint=0` samples at `at=3001`, `4002`, `5012` with `contexts=0` and full packet pools, `DONE seq=1`, `READY seq=2` (phase 2 ticket). (`link=1` is down and `link=2` up by the oracle's meaning.)
- Cause, confirmed by an independent replay of the real captures of both runs through the real supervisor (the analysis is summarised in `docs/networking-physical-preparation.md`, section "Supervisor arrival order", PR #341): the firmware prints link rows at once and the three idle samples of a checkpoint together after the third sample. The supervisor clears its rows before the guard-checked command write (about 1.2 s) and recorded every link row that arrived while a command ran; the idle check used the first recorded row's board time as the time the resources had returned. In this run that row was `LINK at=3827`, later than the first sample (`3001`), so the check failed. In the first run the same defect was present but a split USB chunk delivered the boot-time link row after the command (`at=1810`), so it passed against a value that did not mean what the check assumes. It is timing dependent, independent of the code version, and not a board fault.
- The host-address gate of PR #338 (waiting for the static address of `en13` before a peer binds) was therefore never exercised; phase 2 was not reached.

## 5. Offline checks and restoration

- The normal-image boot capture of B5 (`01-second-run/restore/normal-capture/lines.json`, 403 rows) passes the accepted integrated-normal oracles (`initial_checkpoint_rows` 78 rows, `default_state`, `idle_resources`: PASS).
- **Restoration:** the accepted normal image is in flash (written, `Verify successful.` twice), started once, and its boot capture passes the oracles above. No disconnect diagnostic appeared, so the acceptance rule of PR #336 was again not exercised. The board ends the window in the accepted normal state with the DAPLink powered; guard-3 keeps running (deadline about 15:12 UTC).

## 6. What each result proves and does not prove; open points

| Result | Proves | Does not prove |
| --- | --- | --- |
| Board link up, address 192.168.77.2, phase 1 idle baseline clean (second boot of the M5 image) | the link and address handling reproduce on a second boot after a J-Link reset | any data exchange; recovery; repeatability beyond phase 1 |
| B2' matched after a full power cycle of the board | the QSPI flash keeps the accepted normal image without power | |
| Phase 1 rejected by the runner | a runner defect that depends on row arrival order (cause above) | any board fault; nothing about phases 2 to 6 |

Open points: phases 2 to 6 and the first packets after the link returns (ARP) are untested; the second `DENIED` at 10:29Z (a DAPLink mount attempt about 74 minutes after the first window's restore, nobody operating) is unexplained, and the DAPLink was powered without a guard between 10:41Z and 11:13Z (no volume was found mounted at 11:13Z); CI of PR #338 was green for `host`, `psa-crypto`, `links`, `zephyr controller` and `zephyr storage` and red for `zephyr io` and `zephyr runtime` only at the SDK download step (GitHub release 504); the acceptance rule for the disconnect diagnostic (PR #336) has never been exercised on the board.

## 7. Files

Raw files are kept in `01-second-run/` (the evidence parent of the run: identify, program, boot-1, boot-1-safety, restore, consoles, the carrier-check transcript and the B6 transcript), copied without change except that absolute paths of the author's machine were replaced by `<ws>/`, `<notes>/`, `<artifacts>/`, `<workspace>/` and `<home>/` and the user name of the mount listing by `user`; `*.bin` raw UART captures are stored as `*.bin.raw` (the repository ignores `*.bin`). `SHA256SUMS.tsv` lists, for every file except `README.md` and itself, the SHA-256 of the original and of the committed bytes (the original is empty for the two files written for this record, `01-second-run/b3b-carrier-check.txt` was written by a shell loop `tee` during the run and is a transcript, and `b6-offline-checks.txt` is a transcript of the offline results).
