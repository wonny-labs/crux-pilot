# Stage 1 Ethernet bring-up on the RT1060-EVKB: reduced one-boot run of 2026-11-08

This record keeps the raw evidence of the first real hardware use of the Crux management preparation tools on the dedicated EVKB bench: a reduced, **non-qualifying** one-boot run of the M5 network test image (phases 1 to 6 of `docs/networking-physical-preparation.md`), the three stops on the way, and the restoration of the accepted normal image. It does not qualify HTTPS, SSH, SFTP, entropy, persistence or timing, and it is not the three-boot M5 series. It refers to Issue #28 and does not close it.

**Result in one paragraph.** The board's Ethernet link came up and the M5 image reached its first checkpoints (boot, link up with address 192.168.77.2, phase 1 idle baseline, the ticket for phase 2), but the echo phases 2 to 6 did not run: the host-side test peer crashed on `Can't assign requested address` most likely because macOS drops the manual address of the adapter while the link is down after the reset (the real link-down inventory shows no `inet` line) and restores it a moment after the link returns, and the runner started the peer in that gap. The address was present again when it was next inventoried, about a minute later (`06-main-run-program-capture-restore/restore/acquire-nic.txt`, 09:15:33; the link-down inventories without an `inet` line are `05-restore-fixed-driver/restore/03-verify-normal-nic.txt` and `06-main-run-program-capture-restore/restore/03-verify-normal-nic.txt`); no inventory was taken between the reset (09:14:25) and the crash (09:14:31), so its absence at the moment of the bind was not captured and this is the best-fitting explanation, not a recorded fact. The reduced run is therefore **INCONCLUSIVE** for the echo, timeout and close behaviour. The accepted normal image was restored afterwards, verified twice and replayed offline through the accepted oracles.

## 1. Authorisation (relayed through the main session, quotes)

| Date (chat) | Quote | Covers |
| --- | --- | --- |
| 2026-11-08 | "단계 1 먼저 하고 싶어. 케이블 상태 확인할게.보드 조작 모두 승인할게." | all board operations of stage 1 |
| 2026-11-08 | "방안 A로 진행해줘" / "방안 A승인" / "1번으로 진행해줘" / "모두 승인할게" | identity block and reduced mode, replacement mount guard, driver scope, frozen-input rule, narrow checks |
| 2026-11-08 | "자는 동안 필요한 승인은 사전에 미리해줄께. 검토가 필요한건 독립리뷰를 거치고 pass하면 ... 내 승인 기다리지 말고 해줘." | proceed after an independent PASS without waiting |
| 2026-11-08 | "마운트보호명령실행했어. 허락할테니 계속진행해줘." | the owner started the guard; permission for exactly the pyserial 3.5 wheel (PyPI, hash-pinned, new venv outside the repository) |
| 2026-11-08 | "예" | overwrite the unknown flash content with the accepted normal image, without a separate backup (plan revision 4a, section 3, `inputs/plans/stage1-phase-b-plan-r4.md`) |

## 2. Source, artefacts and plan of the run

| Item | Value |
| --- | --- |
| Source of the run | `3ef758d710d4ebdf974f69350ce8c509772a289e` (main after PR #336), frozen checkout clean; the build ran in the Dev Container image `crux-pilot-devcontainer:local` (linux/amd64, `--memory 2g --cpus 2`, `west build -o=-j2`: these build details come from the plan documents in `inputs/plans/`, not from the freeze log), dependency pins as in `inputs/image-manifest-m5-3ef758d.json` |
| M5 manifest | SHA-256 `0c9a71cf981929ca2b688b27528029897c32e77edccbff7bd3ea4e3896601d06` (`inputs/image-manifest-m5-3ef758d.json`) |
| M5 BIN / ELF / configuration | 97,544 bytes `33a1e1b80cfcd66e377437087ac5ff5a1a39204899a5dbd0957f548a235f2284` / 2,255,292 bytes `17c75138ab398ba2951143c7a2e9ba2c15bd068f5dbb2140e389aad2eccc8a31` / 43,797 bytes `8921ef23dc3a443a7f6aa97e235d00f982ad7444951635c3757032266b06c523`; compared with the builds of `1b3ea3a` and `f720c95` the BIN differs only in the 12-byte commit tag (bytes 94136 to 94147 counted from 1, as `cmp -l` prints them; 0-based offsets 94135 to 94146) |
| Plan | `inputs/plan-m5-stage1-3ef758d.json`, `plan_sha256` `1a012d8dc47a078ac258e3234680b11b72aae3ac6ed49f50463157575a7f278d`, runner `2dcc7a37a49fb263460122c9f84392fbc1f1708032a6634e5bff6a5120565670`, adapter `041575af87362d1c5acb1e1ee414f87db12c8a918513a00d775ff1a877d7c0ce`, one nonce `527584622`, status `SOFTWARE_PREPARED_REDUCED_NON_QUALIFYING_HARDWARE_NOT_AUTHORIZED`; approval `inputs/approval-m5-stage1-3ef758d.json` (nine flags true) |
| Normal image (entry and recovery) | integrated normal, source `ec94fbe`, BIN SHA-256 `ad729684749181f0c26853828550187a2c3e60dbb28cf3ee2194a7984141e02f` (107,872 bytes), manifest `07fce1eaa92d279b82857b1109ec0a0b09629fcb361580dc4267cdfe1ff7290c` (`inputs/image-manifest-normal-ec94fbe.json`), rebuilt byte-identical to the accepted M3 image |
| J-Link | Commander V9.86a (`JLink_V986a`, hashes in `tests/target/jlink-tools.json`), probe 602003106, SWD 400 kHz |
| Mount guard | `tests/target/m5_mount_guard.py` SHA-256 `0baad87914ef73f908a60e3fdbcb951e4993a402ee034baec431a578a39e616c`, arm mode, started by the owner with sudo, log `inputs/guard-2.log` |
| Bench identity | adapter AX88179 `en13` MAC `00:24:9b:6e:f7:7f`, service `AX88179 3` (manual 192.168.77.1/24, no router), UART `/dev/cu.usbmodem11402`, DAPLink serial as in `tests/target/m5_bench.py`, board 192.168.77.2, TCP 42420, UDP 42421, EVKB rev B `mimxrt1060_evk@B//qspi` |
| Host tools | pyserial 3.5 (wheel SHA-256 `c4451db6ba391ca6ca299fb3ec7bae67a5c55dde170964c7a14ceefec02f2cf0`, `--require-hashes`), Python 3.9.6 |

Earlier freezes of the same image code, void after the fixes below: `f720c95` (BIN `eacfd933eccd3bfa20a4e51044a3c7af5ab8fafd907f9d2099cdc4f62cc221fc`) and `1b3ea3a` (BIN `62cb9e8cd4087c80c56e72c959daa45bd5ef7dcbd7087baf56cfe8eb9d746bba`); their manifests are in `inputs/`.

## 3. Timeline (UTC, system clock of the host)

| Time | Event | Result | Evidence |
| --- | --- | --- | --- |
| 06:41 | owner starts the arm-mode guard, DAPLink enumerates, guard vetoes the mount and reports READY | OK (no power command by the agent) | `inputs/guard-2.log` |
| 06:43:07 | B2 `identify` on the first freeze (`f720c95`) | stopped before any J-Link command: `Exclusive UART ownership changed` (the failing check kept no output, so the cause was found afterwards: `lsof -Fp` prints `f<fd>` lines and `lsof` exits 1 when the never-opened `tty` node is named; the real answers of the fixed check are the `01-identity-only-owners-cu.txt` and `...-tty.txt` files of `02-identify-found-unknown-firmware/identify/`) | `01-first-identify-stopped-lsof/` |
| 08:01:32 | B2 `identify` on the second freeze (`1b3ea3a`, after PR #333) | identity matched (CPUID `0x411FC271`, DPIDR `0x0BD11477`, S/N 602003106, VTref 3.322 V); `verifybin` of the normal image: `Verify failed @ address 0x60001024`, Commander exit non-zero, driver stopped: the flash held another image (probably the PSA test image of 2026-11-07, not proven) | `02-identify-found-unknown-firmware/` |
| 08:21:25 | R0 `restore` (owner "예"), driver of `1b3ea3a` | flash written (0x60000000 to 0x6001FFFF), `Verify successful.`, then `****** Error: Could not start CPU core. (ErrorCode: -1)` after `q` with exit 0; the M3 wrapper stopped | `03-restore-stopped-diagnostic/` |
| 08:27:15 | read-only `identify` (plan amendment 4b, `inputs/plans/stage1-phase-b-plan-r4b.md`) | matched byte for byte (second verification); `IsHalted`: `CPU is halted (PC = 0x600156C2).` | `04-identify-matched/` |
| 09:12:26 | R0' `restore` with the fixed driver (PR #336, freeze `3ef758d`) | write skipped (`Contents already match`), `Verify successful.` twice, one start, 15 s capture shows the normal boot banner | `05-restore-fixed-driver/` |
| 09:13:15 | B2' `identify` | matched | `06-main-run-program-capture-restore/identify/` |
| 09:13:33 | B3 `program` of the M5 image | written (131,072 bytes affected, 6.5 s), `Verify successful.`, reset, start: `M5_PROGRAMMED_VERIFIED_STARTED` | `06-.../program/` |
| 09:13:58 | B3b carrier check | `en13` active with `inet 192.168.77.1`, `route -n get 192.168.77.2` via `en13` (flags `<UP,DONE,CLONING,STATIC>`) | `06-main-run-program-capture-restore/b3b-carrier-check.txt` (transcript of the console output), and the capture's own `boot-1-safety/lease-1-*` files |
| 09:14:15 | B4 reduced capture `--boot 1 --through-phase 6` | one reset at 09:14:25; phase 1 completed; phase 2 peer crashed at 09:14:31 (`Errno 49 Can't assign requested address`); `ABORT_INCONCLUSIVE` | `06-.../boot-1/`, `boot-1-safety/` |
| 09:15:31 | B5 `restore` | normal image written (131,072 bytes affected, 7.0 s), `Verify successful.` twice, one start, 15 s capture shows `M3_BOOT source=ec94fbec22cd profile=integrated commands=0 fault_default=off` | `06-.../restore/` |
| 09:17 | B6 offline checks | see section 5 | `06-main-run-program-capture-restore/b6-offline-checks.txt` |

## 4. What the M5 run showed (from the board's own UART, `boot-1/uart-rx.bin.raw`)

- `*** Booting Zephyr OS build v4.4.2 ***`, `M5_BOOT source=3ef758d710d4`, `M5 ARM token=527584622`, `M5 READY seq=1`.
- `M5 LINK ... link=1 ... address=1 ipv4=3232255234` at 1.8 s, then `link=2` at 3.8 s (by the oracle's meaning in `tests/target/m5_network_oracle.py`, `link=1` is down and `link=2` is up); `ipv4=3232255234` is 192.168.77.2.
- Three `M5 IDLE` checkpoints with `contexts=0`, full packet pools (`rx_pkt=16 tx_pkt=16 rx_buf=32 tx_buf=32`), `M5 DONE seq=1`, and `M5 READY seq=2` (the phase 2 ticket) at 5.1 s.
- Host side: the capture's first lease check saw the dedicated NIC active with the static address and the direct route; the phase 2 peer (`boot-1/peer-2-process.log`) died on `bind((192.168.77.1, 42420))`: `[Errno 49] Can't assign requested address`; `boot-1/result.jsonl`: `ABORT_INCONCLUSIVE`, `target_may_still_execute: true`, `automatic_reset: false`.

## 5. Offline checks (B6) and restoration

- `capture_m5_network.py uart-replay` of `boot-1`: exit 0 (the raw M5 boot replays into the rows above).
- The normal-image boot captures of R0' (`05-.../restore/normal-capture/lines.json`) and of B5 (`06-.../restore/normal-capture/lines.json`), 397 rows each, were replayed through the accepted integrated-normal oracles (`capture_m3_integrated.initial_checkpoint_rows` with commands 0 giving 78 rows, `m3_integrated_oracle.default_state`, `m3_integrated_oracle.idle_resources`): all three PASS for both captures.
- **Restoration:** the accepted normal image is in flash (written, `Verify successful.` twice), started once, and its boot capture passes the oracles above. The board ends the run in the accepted normal state with the DAPLink powered and the guard still running (its lifetime ends about 10:41 UTC).

## 6. What each result proves and does not prove

| Result | Proves | Does not prove |
| --- | --- | --- |
| Board link up, address 192.168.77.2, phase 1 idle baseline, phase 2 ticket (one boot of the M5 image) | on this bench the Ethernet PHY and the M5 image's link and address handling work for one boot after a J-Link reset | any data exchange; recovery; repeatability |
| Host carrier and route on `en13` after the programming start | the dedicated cable and adapter give carrier and the connected route | the echo behaviour |
| Phase 2 peer crash `Errno 49` | the host could not bind 192.168.77.1 at that moment (most likely the manual address of `en13` was briefly absent after the link returned; not captured) | a board or firmware fault; nothing about phases 2 to 6 |
| Identify, `verifybin` match and mismatch, restore, program transcripts | the driver, wrapper and parsers work against the real J-Link V9.86a output for these commands | `IsHalted` after an accepted disconnect diagnostic beyond the single sample |
| Normal-image captures pass the accepted oracles | the restored image behaves as the accepted normal image for its first seconds | long-run behaviour |

## 7. Fixes made on the way (all merged before this record)

| PR | Merged as | Fix |
| --- | --- | --- |
| #322, #323, #326, #327, #328 | `76c9f74`, `056ed78`, `35e67df`, `321c66b`, `fc9a996` | reviewed bench identity block and reduced run, replacement mount guard with arm mode, programming and restoration driver, frozen-input rule |
| #333 | `1b3ea3a` | `lsof` ownership checks (the `f<fd>` lines and the exit code 1 of an unopened node), link-down `ifconfig` without `inet` line, fixtures of real host output and the audit table in `docs/networking-physical-preparation.md` |
| #336 | `3ef758d` | the documented J-Link disconnect diagnostic after a verified load is accepted in the restore driver only, with the real transcripts as fixtures |

## 8. Open points

- The echo, silent-peer, early-close phases (2 to 6) are untested on the board: the runner needs a bounded wait for the host address of the dedicated adapter before it starts a peer (a host-only change, not made here), then a new reduced run.
- Still unknown: whether the disconnect diagnostic of a `loadfile`/`verifybin`/`q` step shows up again. Two later samples did not show it: R0' skipped the write and B5 wrote 131,072 bytes (`06-main-run-program-capture-restore/restore/02-load-normal.log`); there is no `*.diagnostic-accepted.json` anywhere in this record, so the acceptance rule of PR #336 was never exercised on the board. The real `ifconfig` and `route` output of the active dedicated adapter is in this record (`boot-1-safety/lease-1-interface.txt`, `lease-1-carrier.txt`, `lease-1-route.txt`, and the `*-nic.txt` files).
- A proposed follow-up for Issue #28 (not posted): the reduced run reached the link and idle baseline and stopped on a host race before the echo phases.

## 9. Order of the independent checks

The independent check of plan revision 5 handed back its PASS (no blocking finding) at 09:12:03Z, before the first board step that used the plan (R0', 09:12:26Z); the notes it contained were appended to the plan file at 09:12:17Z. The other plans each had their independent PASS before the board step that used them: r3 at 08:00:28Z (B2 at 08:01:32), r4a at 08:09:13Z after a REVISE at 08:08:22Z (R0 at 08:21:25), r4b at 08:27:05Z (identify at 08:27:15). The times are those of the reviewers' hand-back messages.

**Correction after the run, stated openly:** the plan r5 file was edited once more at 09:29:49Z, after the run, in a single place: the heading of its section 6, which read `Independent check (Opus, read-only, 2026-11-08T09:2xZ): PASS ...` (the placeholder `09:2xZ` was a mistake) and now reads `... 2026-11-08, received before 09:12Z (R0 prime started 09:12:26Z) ...`. The committed copy `inputs/plans/stage1-phase-b-plan-r5.md` is the corrected file; the run-time version was overwritten in place and is not retained, so its row in `SHA256SUMS.tsv` has no original hash and is marked as corrected after the run. The accurate check time is the 09:12:03Z hand-back above, not the corrected heading's wording. The plan documents are session notes of the author; editing them after the fact is recorded here and reported to the main session.

## 10. Files

Raw files are kept per run in numbered folders, copied without change except that absolute paths of the author's machine were replaced by `<ws>/`, `<artifacts>/`, `<workspace>/` and `<home>/` and the user name of the mount listing by `user`. `*.bin` raw UART captures are stored as `*.bin.raw` (the repository ignores `*.bin`) with unchanged bytes. `SHA256SUMS.tsv` lists, for every file except `README.md` and itself, the SHA-256 of the original (empty for files written for this record: `b3b-carrier-check.txt`, `b6-offline-checks.txt`) and of the committed bytes and whether it was redacted. The plan documents of the run (`inputs/plans/`, revisions 2 to 5 and the amendment 4b) and the guard session record `inputs/session-2.json` are included, redacted the same way. The stopped runs keep their `failure.json`, console output and J-Link scripts and logs.
