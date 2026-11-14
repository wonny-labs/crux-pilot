# PSA Crypto conformance image on MIMXRT1060-EVKB: board record (2026-11-07)

Issue: wonny-labs/crux-pilot#142. The board plan (revision 3) and the executor (`hw-run.py`) lived outside this repository, in a local working
folder that is not a git repository, so no commit hash exists for them and they cannot be reviewed from here. The J-Link command files and
logs, the host-timestamped captures (`*.times`) and the flash backup also stay with the operator.

Files here:
- `uart-boot1.txt` to `uart-boot3.txt`: the three gated reset boots, console bytes as received (13,598 bytes each, 202 lines with CR LF).
  `run-2-result.json` counts 13,396 per boot because it counts after removing the CRs; `run-2-events.log` counts the after-flash run with them.
- `uart-after-flash.txt`: the run the image starts by itself after programming (informational, not a boot).
- `run-2-result.json`, `run-2-events.log`, `run-2-inputs.txt`: run 2. `run-1-events.log`, `run-1-result.json`: run 1, which stopped at the
  silence gate (see below). Local paths are replaced by `<psa-board>`.

## What ran
Facts in this section come from the operator's command files and logs, not from files kept here, except the image hash and the console text.
- Board: MIMXRT1060-EVKB rev B, QSPI internal boot, external J-Link PLUS S/N 602003106 (SWD, 400 kHz, device `MIMXRT1062xxx6A`), SEGGER
  JLinkExe V9.86a (the executor requires that path to resolve to the V9.86a installation).
- Image: `zephyr.bin` 120,384 B, SHA-256 `31e3b8f3d04c3dbae58df27c923d8def106f86ffe81161314e1e04ca5e16ccce`, built from a worktree at
  `3e0a5d7280e42df4415ca1f685ba3ad4b61775b4` with Zephyr v4.4.2 (TF-PSA-Crypto via the Mbed TLS module). The worktree was clean when the run
  happened; its state at build time was not recorded. Programmed at 0x60000000 and verified with `verifybin`.
- Console: DAPLink VCOM, 115200, read-only.

## Pass criteria, fixed in the plan before the run
`PROJECT EXECUTION SUCCESSFUL`; `pass = 43, fail = 0, skip = 3, total = 46` (45 cases plus `test_case_list_is_complete`); one
`PSA_RANDOM_PROBE <32 hex digits>` line; and the three probe values differ.

## Result (run 2)
| Boot | Result | Summary | `PSA_RANDOM_PROBE` |
|---|---|---|---|
| 1 | PROJECT EXECUTION SUCCESSFUL | pass 43, fail 0, skip 3, total 46 | `52c8082c1e0a63b4a644f7528503f385` |
| 2 | PROJECT EXECUTION SUCCESSFUL | pass 43, fail 0, skip 3, total 46 | `3fe91c72958a3a5810fcd002c8396d69` |
| 3 | PROJECT EXECUTION SUCCESSFUL | pass 43, fail 0, skip 3, total 46 | `bf38c6ed99fd2a80749a47697ff65bfe` |

All three boots meet the criteria. The three skips are the declared Ed25519 gaps of TF-PSA-Crypto. The run that the image started by itself
after programming also ended in PROJECT EXECUTION SUCCESSFUL, with a fourth, different probe value (informational, not counted as a boot).

## What happened in run 1
Run 1 programmed and verified the image, then stopped at the silence gate before boot 1: bytes arrived on the port before the reset. This is the
operator's observation: the bytes were the freshly programmed image running (J-Link leaves it running when it exits), not a foreign writer. The
bytes of run 1 were not kept (only `run-1-events.log` and `run-1-result.json`). The gate and the plan were corrected (revision 3, step 2b),
checked by an independent AI reviewer whose reports are not kept in the repository (REVISE, then ESCALATE on an error in the plan's approval
record, which was corrected), and run 2 was started with the owner's go-ahead.

## Not covered
- Power-on boot (all three boots are warm resets by J-Link).
- TRNG: the probe values are PSA random output read after the DRBG. Three differing 16-byte values show that the output does not repeat. They do
  not show that the TRNG was the seed source, and they do not measure quality.
- One board, one image build.
- The previous board image was not restored; the board now runs the PSA test image. The only copy of the original first 128 KB of flash is
  the operator's `rt1060-run-1/backup-0x60000000-128k.bin` (SHA-256 `f6b37d4c00872a8e10cb852522452863abe2263b3eb4dac73c631ff8ffa59c1a`).
- Reseeding and the entropy-failure path (a real TRNG read failure cannot be induced here); the fail-closed behaviour is covered on native_sim in CI.
- Secure storage and key persistence, TLS, `psa-arch-tests`, concurrency, and a consumer calling the provider.
- The executor and plan revision 3 are not in the repository; the independent review of them had no git SHAs.
