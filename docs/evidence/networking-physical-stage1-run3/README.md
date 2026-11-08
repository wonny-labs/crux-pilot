# Stage 1 Ethernet bring-up on the RT1060-EVKB: third reduced one-boot run of 2026-11-08 (phases 1 to 6 completed)

Third hardware window of the reduced, **non-qualifying** one-boot run of the M5 network test image of `docs/networking-physical-preparation.md` (phases 1 to 6: idle baseline, TCP and connected-UDP echo ten times, silent peer, echo after the timeout, early TCP close with UDP echo, echo after the early close). The first run stopped on a host-address race at phase 2 (`docs/evidence/networking-physical-stage1/`), the second on an arrival-order defect of the runner's idle check in phase 1 (`docs/evidence/networking-physical-stage1-run2/`). This record keeps the raw evidence of the third window, in which the runner completed phases 1 to 6, and the restoration of the accepted normal image. It does **not** qualify HTTPS, SSH, SFTP, entropy, persistence or timing of the management stack, it is not the three-boot M5 series (no link loss, recovery or final idle phase), and it refers to Issue #28 without closing it.

**Result in one paragraph.** On this bench, for one boot of the M5 image, the Ethernet link came up, the board got its static address, and the runner completed phases 1 to 6 (`REDUCED_BOOT_PROTOCOL_COMPLETE_NON_QUALIFYING`): ten TCP and ten UDP echo transactions answered (TCP 4 to 17 ms, UDP 1 to 7 ms), the silent-peer phase timed out as expected (`-116` after 2000 ms), echo after the timeout and echo after the early close answered, the early TCP close gave `-104` (ECONNRESET) with the UDP echo answered, every transaction closed its sockets and returned its resources (idle checkpoints passed). The host-address gate added after the first run was needed and worked: at phase 2 it saw the adapter active with the address not yet restored (`host_pending`) twice and started the peer after 0.54 s. The accepted normal image was then restored (written, verified twice, started) and replayed offline through the accepted oracles.

## 1. Authorisation

The owner's approval of the stage 1 board operations and the standing approval to proceed after independent PASS (relayed by the main session; quotes in the first record), the owner's re-arm of guard-3 with sudo, and the main session's instruction of 2026-11-08 to continue with the fix PRs, a re-freeze, a new plan and its independent check, and to start the board window no later than 14:30 UTC. The owner's relayed decision "지워서 진행해줘" covered deleting the author's own merged worktrees and scratch copies to restore the disk floor (the author's read-only inventory at about 12:50Z showed 3.83 GiB free against the 5 GB floor of the plan, and 7.98 GiB after the deletion; that inventory was not saved as a file in this record). The plan's independent check had handed back its PASS before that and no plan content changed.

## 2. Source, artefacts and plan

| Item | Value |
| --- | --- |
| Source | `94a4a7da879f41e9b0b720e661bfab104810d4f0` (main after PR #341), frozen checkout clean; `tests/`, `tests/target` and `docs/networking-physical-preparation.md` equal the reviewed PR head `c3ba3133` (only the two earlier evidence directories differ) |
| M5 manifest | SHA-256 `c8171c1fcd4b69692a1316296b73b4e4eda0315ac00984cf07f4eb064b8bfed0` (`inputs/image-manifest-m5-94a4a7d.json`) |
| M5 BIN / ELF / configuration | 97,544 bytes `d4a59b032133096b6ee2744af4ed30c1142b54986ffb5c2763434793873195bf` / 2,255,292 bytes `b736ccd6f001826aebb8b25a43a817485515227b147ba5cc7629e3e7e02664b7` / 43,797 bytes `f925c7a9c0771cfbf6394b4d8efeb0797625fff6ef4daeb3e8c9c4968ec673d6`; compared with the build of `b44b66a` the BIN differs only in the commit tag (bytes 94136 to 94147 counted from 1; 11 of the 12 tag bytes differ) |
| Plan | `inputs/plan-m5-stage1-94a4a7d.json`, `plan_sha256` `d9d69b37c4840e48ce504143539918c40eda65695e6b00fedce80778efe0418a`, runner `27f426ae21d543ee15e341e09572501c357a41a9f9661a3eb9820be28251f8f9` (changed by PR #341), adapter `e8c446b4404b7fff0a0d139c4f18a4f8942fe200a347f8205944e8fd3595d809` (PR #338), one nonce `671604249`, reduced through phase 6, non-qualifying; approval `inputs/approval-m5-stage1-94a4a7d.json`; plan document `inputs/plans/stage1-phase-b-plan-r7.md` (an unchanged original apart from redaction; its header still says `not yet independently re-checked`, and its section 6 records the independent PASS, which was handed back before the run; the file was created at 12:43:14Z and last written at 12:52:49Z, before B2' at 12:53:04Z) |
| Normal image, J-Link, bench identity | as in the first record (normal image `ad729684749181f0c26853828550187a2c3e60dbb28cf3ee2194a7984141e02f`, manifest `07fce1ea…290c`, J-Link V9.86a, probe 602003106, `en13`, MAC and service as in `tests/target/m5_bench.py`) |
| Guard | guard-3, arm mode, started by the owner with sudo from the byte-identical guard script (SHA-256 `0baad879…616c`): started and `ARMED` at 11:12:37Z, `DENIED exact_DAPLink_USB_serial=… disk=disk4` at about 11:12:51Z, `READY` at about 11:12:55Z (deadline about 15:12Z); `inputs/guard-3.log`, `inputs/session-3.json` |

## 3. Timeline (UTC, system clock of the host)

| Time | Event | Result | Evidence |
| --- | --- | --- | --- |
| about 12:52:49 | B1 read-only inventory (disk, guard, adapter, ownership); its output was not saved | stopped nothing | (no file) |
| 12:53:04 | B2' read-only `identify` of the normal image | matched | `01-third-run/identify/` |
| 12:53:38 | B3 `program` of the M5 image | written (131,072 bytes affected, 6.5 s), `Verify successful.`, reset, start | `01-third-run/program/` |
| 12:54:09 | B3b carrier check | `en13` active with `inet 192.168.77.1`, route via `en13` (flags `<UP,DONE,CLONING,STATIC>`) | `01-third-run/b3b-carrier-check.txt` (written during the run by a shell loop with `tee`; a transcript, not tool output) |
| 12:54:09 | B4 reduced capture `--boot 1 --through-phase 6` | preflight, one approved reset, phases 1 to 6; finished 12:55:35 with `REDUCED_BOOT_PROTOCOL_COMPLETE_NON_QUALIFYING` | `01-third-run/boot-1/`, `boot-1-safety/` |
| 12:56:00 | B5 `restore` | normal image written (131,072 bytes affected, 7.0 s), `Verify successful.` twice, one start, 15 s capture shows the normal boot banner | `01-third-run/restore/` |
| about 12:57 | B6 offline checks | see section 5 | `01-third-run/b6-offline-checks.txt` |

Before the window: guard-3 logged a third `DENIED` of a DAPLink mount attempt at about 12:31:19Z with nobody operating the board (section 6).

## 4. What the capture showed (`01-third-run/boot-1/`, from the board's own UART and the peer files)

- Board UART (`uart-rx.bin.raw`, 161 lines): `*** Booting Zephyr`, `M5_BOOT source=94a4a7da879f`, `ARM`, `READY seq=1`, `LINK seq=0 at=1810 link=1`, `LINK seq=1 at=3821 link=2 admin=1 address=1 ipv4=3232255234` (192.168.77.2; `link=1` is down and `link=2` up by the oracle's meaning), idle checkpoints, and for phases 2 to 6 the `BEGIN`/`END` rows of every transaction followed by `IDLE` samples and `DONE`, each phase ending with the `READY` of the next.
- Phase 1 (idle baseline, `DONE at=5058`): passed with the arrival-order fix of PR #341.
- Phase 2 (echo, 10 rounds, TCP and UDP): 20 `END` rows, all `result=0`; TCP 4 to 17 ms, UDP 1 to 7 ms; `close_error=0`, `owned=0`, `peak=1` everywhere; `DONE at=47997`. Peer file `peer-2.jsonl`: 20 requests, 20 exchanges, all answered.
- Phase 3 (silent peer): TCP and UDP `result=-116` (ETIMEDOUT) after 2000 ms each; peer: 2 requests, 2 exchanges, none answered.
- Phase 4 (echo after the timeout): TCP and UDP `result=0`; peer: both answered.
- Phase 5 (early TCP close, UDP echo): TCP `result=-104` (ECONNRESET), UDP `result=0`; peer: two exchanges, one answered (the UDP echo).
- Phase 6 (echo after the early close): `result=0` for both; peer answered both.
- Peers: five peer processes, each reaped (`peer-stops.jsonl`). Host-address gate (`peer-gate.jsonl`, `peer-gate/nic-*.txt`): phase 2 `host_pending`, `host_pending`, `host_up`, peer started after 0.544 s; phases 3 to 6 `host_up` at the first observation (0.028 to 0.038 s).
- The runner's own per-phase checks (idle samples at least 1000 ms apart with full pools and stack headroom, transaction order and clocks, symbolic results, peer wire identity) passed; `result.jsonl` is `REDUCED_BOOT_PROTOCOL_COMPLETE_NON_QUALIFYING`, `qualifying: false`, `satisfies_m5_series: false`.

## 5. Offline checks and restoration

- `capture_m5_network.py uart-replay` of `boot-1` (with the raw capture renamed back to `.bin`): exit 0, 160 records, last `READY seq=7`.
- The normal-image boot capture of B5 (`01-third-run/restore/normal-capture/lines.json`, 397 rows) passes the accepted integrated-normal oracles (`initial_checkpoint_rows` 78 rows, `default_state`, `idle_resources`: PASS).
- **Restoration:** the accepted normal image is in flash (written, `Verify successful.` twice), started once, and its boot capture passes the oracles above. No disconnect diagnostic appeared in this window either, so the acceptance rule of PR #336 was again not exercised. The board ends the window in the accepted normal state with the DAPLink powered; guard-3 keeps running (deadline about 15:12 UTC).

## 6. What each result proves and does not prove; open points

| Result | Proves | Does not prove |
| --- | --- | --- |
| Phases 1 to 6 completed in one boot (reduced, non-qualifying) | on this bench (board, cable, AX88179 `en13`) the M5 image reaches link up with its static address, answers a host peer over TCP and connected UDP ten times with the fixed expected outcomes, times out as expected against a silent peer, echoes after the timeout and after an early close, and returns its resources, for one boot | HTTPS, SSH, SFTP, entropy, persistence or timing of the management stack; link loss and recovery (phases 7 to 9); repeatability over three boots; worst-case timing or stack margins; the integrated image of PR #313 |
| `host_pending` at phase 2 | after the reset's link flap macOS shows the adapter active without its manual address for a short time (observed twice, 0.54 s in all); a bind failure did not happen in this run because the gate waited, so the first run's `Errno 49` stop is consistent with this state, an inference from the first record | how long this state lasts in general |
| B2' matched after J1 was unplugged and replugged (guard-3 re-arm) and across three board windows | the QSPI flash held the accepted normal image | |
| Restore capture passes the accepted oracles | the restored image behaves as the accepted normal image for its first seconds | long-run behaviour |

Open points: phases 7 to 9 and the three-boot series are not run; the first packets after the link returns (ARP) worked in practice (the first TCP transaction of phase 2 took 5 ms) but were not observed separately; the acceptance rule for the disconnect diagnostic (PR #336) has never been exercised on the board; the guard logged `DENIED` events of DAPLink mount attempts with nobody operating the board: guard-2 at about 10:29Z (about 74 minutes after the first window's restore) and guard-3 at about 12:31Z (about 74 minutes after the second window's restore); no such event followed this window's restore before this record was written (about 12:57Z); the cause is unknown, the guard vetoed each, and a re-enumeration during a step would stop the run through the ownership and USB checks (fails safe).

## 7. Files

Raw files are kept in `01-third-run/` (the evidence parent of the run: identify, program, boot-1, boot-1-safety, restore, consoles, the carrier-check transcript and the B6 transcript), copied without change except that absolute paths of the author's machine were replaced by `<ws>/`, `<artifacts>/` and `<workspace>/` and the user name of the mount listing by `user`; `*.bin` raw UART captures are stored as `*.bin.raw` (the repository ignores `*.bin`; state `binary-unchanged` in the checksum list). `SHA256SUMS.tsv` lists, for every file except `README.md` and itself, the SHA-256 of the original and of the committed bytes (the original is empty only for `01-third-run/b6-offline-checks.txt`, the one file written for this record, a transcript of the offline results; `01-third-run/b3b-carrier-check.txt` is kept as an unchanged original).
