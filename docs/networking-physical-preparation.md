# Issue #28 physical networking preparation

Status: software preparation only. No physical result, programming, reset, cable operation, merge or Issue closure is authorised by this document. Baseline is merged PR #38 (`df52a62c0475fe87e67bde1fa50c49090a146936`). The approved BSD/POSIX client profile and read-only primary-interface contract remain unchanged.

## Test composition and evidence scope

`tests/networking_physical` is a dedicated composition, not a production application. `transaction.c` uses only the approved standard profile. Native context/pool/stack observation, static address setup and the observer thread belong to this composition. Existing `samples/networking/client.[ch]` and runtime-port consumers remain unchanged. Physical results would qualify this private M5 consumer; they would not demonstrate physical execution of those unchanged consumers.

M5N1 requests are exactly 32 bytes: ASCII `M5N1`, nonzero nonce32, phase16, protocol16 (TCP=1, UDP=2), transaction32, and 16 bytes `crux-m5-echo-v1\0`; integers use network byte order. Each response echoes the complete request. M3 application frames and M3 physical results are not M5 evidence. Shared clock/image/UART safety helpers retain their separate purpose.

Three fresh boots must have distinct externally selected nonces, one boot banner each, and the exact identified source. Each boot runs the following sequence with no intervening reset:

| Phase | Command | Peer | Pairs |
|---|---|---|---:|
| 1 | I | initial full idle baseline | 0 |
| 2 | P | TCP/connected UDP echo | 10 |
| 3 | P | receive request, remain silent | 1 |
| 4 | P | echo after timeout | 1 |
| 5 | P | TCP closes after complete request; UDP echoes | 1 |
| 6 | P | echo after early close | 1 |
| 7 | L | silent TCP, approved manual disconnect; UDP attempted while down | 1 |
| 8 | P | echo after confirmed link and route restoration, same boot | 1 |
| 9 | I | final full idle baseline | 0 |

This is 16 sequential TCP/UDP pairs per boot, 48 pairs and 96 calls total. Only one command may be outstanding. A fresh target readiness ticket is consumed before every non-ARM command and issued after completion and RX drain. Queued, stale or malformed input stops application traffic. There is no automatic application traffic at boot or after failure.

## Fixed acceptance proposal

These values are proposed short-test criteria requiring explicit physical approval; they are neither measured RT1060 results nor worst-case guarantees.

| Quantity | Criterion |
|---|---|
| Functional connect/send/receive budget | 2,000 ms, single monotonic deadline |
| Observed complete call latency | record entry, functional completion before close, and return; >2,500 ms requires review and stops qualification |
| External host liveness | 10,000 ms command-to-BEGIN and BEGIN-to-END receipt; unrelated records do not renew it |
| Per-command / series absolute supervision | 150,000 ms / 1,800,000 ms; no interactive extension |
| Operator input window | at most 90 s, clamped to remaining absolute budget |
| Link restoration observation | within 15 s after acknowledged reconnect; timeout is inconclusive and cannot authorise reset |
| Resource settling | within 5,000 ms after return, three complete snapshots at least 1,000 ms apart |
| Idle resources | contexts 0; packet RX/TX free 16/16; data buffers free 32/32 |
| Capacity | contexts/connections 4/4; packets 16/16; data buffers 32/32; no capacity increase |
| Main usable stack | 4,096 bytes; unused >=1,024 bytes |
| Observer usable stack | 2,048 bytes; unused >=512 bytes |
| Observer interval | nominal 50 ms; actual timestamps are evidence, not guaranteed edge precision |

The 2,000 ms functional deadline does **not** establish a 2,100 ms whole-call return bound. Cleanup and scheduling latency are measured separately. The 2,500 ms threshold is an observed acceptance threshold. External liveness expiry means ABORT/INCONCLUSIVE: the target may still be executing, and host timeout does not cancel it. No reset or retry follows automatically.

There are no application workers beyond main and the identified observer. Stack watermark observations describe this finite trial, not all future executions. Native network/kernel workers retain the pinned Zephyr configuration; this slice does not claim comprehensive system WCET or stack qualification.

## Link event and recovery procedure

The peer must first record a complete M5 phase-7 TCP request. The operator may then receive a disconnect cue for that exact nonce/phase/transaction. Capture the cue, operator acknowledgement, dedicated host NIC carrier DOWN and target primary-interface DOWN. The target DOWN timestamp must fall between TCP entry and functional completion. Missing confirmation or a late DOWN is **INCONCLUSIVE**, even when a timeout occurs. Do not extend the functional deadline to accommodate human reaction.

This stage qualifies TCP in-flight loss and a subsequent UDP attempt while the link is already down. It does **not** qualify UDP in-flight loss. If independent UDP in-flight loss is required, prepare and approve a separate stage rather than relabel this evidence.

After phase 7 ends and settles, separately authorised cable reconnection must be acknowledged. Require host carrier UP, exact isolated route, and target primary-interface UP before phase 8. Phase 8 uses fresh sockets in the same nonce/boot with no reset. A recovery requiring reset is a distinct, nonqualifying outcome for this resetless criterion; reset requires its own approval and new evidence series.

UART main and observer streams share a record lock, but observation timestamps precede output. Do not require globally increasing timestamps across threads. Main lifecycle and observer sequence each retain order. Host receipt times and target monotonic times are separate domains; use explicit event correlation, never direct clock subtraction across devices.

## Topology and safety gates

Use the accepted M3 isolated topology only after fresh verification: RT1060 EVKB revision B, QSPI image base `0x60000000`, 8 MiB geometry; board `192.168.77.2/24`, dedicated AX88179 peer `192.168.77.1/24`, TCP 42420 and UDP 42421. The unchanged M3 overlay gives GPIO1.9 exclusively to PHY reset, uses fixed test MAC `02:43:52:55:58:03`, and polls the PHY every 500 ms with its IRQ path excluded. The host adapter, route and USB identities must match the approved inventory. No alternate interface, default route, bridge, Internet path, DHCP or runtime address change is part of this test. Netif status observes the configured primary interface; it neither binds sockets nor proves routing or peer reachability. Host peer composition explicitly owns SIGPIPE policy; reusable consumer code changes no process policy.

Before any physical operation, bind written approval to the final source and runner revision, independently verified manifest hash, exact ELF/BIN/configuration hashes and the identified normal entry/recovery images. Obtain fresh target/probe/UART ownership, root USB mount guard, isolated interface/route checks and exclusive debugger/serial ownership using the accepted M3 procedures. Preserve J-Link executable/DLL hashes, probe serial `602003106`, firmware-update disabling, explicit selection and bounded process handling. Unknown firmware maintenance, transport, mount or ownership state is a hard stop.

Each operation needs explicit authorisation:

1. Read-only inventory, guard and ownership checks; probe connection and full entry-image verification are separately enumerated physical access.
2. Program the exact M5 image using the accepted M3 loader/verify sequence. Account for the loader's implicit reset/start in this permission; capture this prefix separately from qualification boots.
3. For each boot, separately enumerate the read-only qualification preflight (identity-only probe connection and full current M5 BIN verification) and one fresh qualification reset. The preflight must pass before reset. No automatic retry or unlisted reset.
4. Each phase-7 disconnect and reconnect on the dedicated cable, with matching boot nonce and phase.
5. Any reset-based recovery, if later needed, under new approval; it cannot satisfy resetless recovery.
6. Final restoration and full verification of one exact accepted normal image, plus its accepted normal-behaviour checks. Missing entry/recovery artefacts or uncertain state blocks execution.

Historical integrated normal BIN `ad729684749181f0c26853828550187a2c3e60dbb28cf3ee2194a7984141e02f` (107,872 bytes, source `ec94fbe`) is a reference identity, not evidence of current board contents or sufficient restoration authority. Resolve the actual accepted manifests and complete artefacts before execution approval.

No writable native Flash/storage is activated; no production partition/layout is changed. Known functional failure may permit only the specifically preapproved normal restoration while all safety gates remain valid. Unknown target/probe state permits no further hardware changes. Preserve incomplete raw records and classify them, rather than deleting or treating absence as success.

## Software evidence and exact artefacts

The final evidence index and manifest identify executed checks, build inputs, source identity, ELF/BIN/configuration hashes and independent review. Software tests use Host loopback or Zephyr native simulation; RT1060 cross-build proves compilation and image structure only. Physical acceptance remains pending.

## Prepared runner workflow

`capture_m5_network.py plan` generates the three-boot operation/command plan and an unauthorised approval template. It does not execute that plan. `check-approval` checks exact document bindings and individual operation flags; this is not a live ownership/guard check. `replay` requires retained raw UART bytes and continuous receive/poll records, rather than trusting embedded parsed UART data.

The transport-independent `Supervisor`, bounded `run_boot`, and concrete `m5_capture_adapter.py` handle exact raw UART input/output, peer subprocess launch/stop/reap, operator receipt files and asynchronous dedicated-NIC/route observations. Software tests inject fake UART, NIC, processes, guard and Commander; they never open the physical devices. Buffered source-event times remain in raw evidence while dispatch time controls live supervision.

The hardware-gated `capture` subcommand verifies exact M5/entry/recovery manifests and runner/adapter hashes before device acquisition. The accepted M3 lease then checks fresh root guard, probe/UART ownership, exact UART USB identity and isolated NIC/service/route. It starts the continuous receiver and drains stale input before the separately approved preflight and fresh reset. Identity-only `q` and exact current M5 `verifybin` must pass before `r/g/q`; wrong or unknown identity, voltage or image blocks reset. Reset requires **both** `--execute-hardware` and `--perform-approved-reset`, plus the exact `qualification_preflight_N` and `qualification_reset_N` approvals. The same lease owner invokes the accepted M3 `r/g/q` Commander sequence with identity/voltage checks and 120-second pre-guard/45-second post-guard series reserves. This avoids a competing programmer/UART owner. There is no automatic reset retry, programming, restoration or cable control. No such invocation was made during preparation.

Native reads/writes and subprocesses are bounded. Guard checks complete before a fresh deadline check immediately before each UART write. Unknown/absent/ambiguous carrier status is an error, not DOWN. Startup and shutdown failures independently attempt receiver, peer, serial, NIC and lease cleanup; unknown target state grants no reset or restoration authority. A persistent session record prevents three separate CLI invocations from silently restarting the 1,800-second series budget.

After L7 DONE/READY8, stop and reap its silent peer before reconnect or binding the recovery peer. The loss peer intentionally expects only TCP on the wire, since UDP is attempted with carrier down. UART receipt can lag peer request timestamps; correlation uses the exact command write, matching identities, target lifecycle and END receipt. It must not infer target execution order solely from host receipt order.

Resource acceptance checks full native pools after every operation and explicit application descriptor ownership. It does not claim sampled native packet/buffer peak usage, sustained-throughput limits or whole-system timing bounds.


Software-only commands (safe preparation/replay):

```sh
python3 tests/target/capture_m5_network.py plan --manifest <M5-manifest> \
  --entry-manifest <accepted-entry-manifest> --recovery-manifest <accepted-normal-manifest> \
  --nonces <N1> <N2> <N3> > operation-plan.json
python3 tests/target/capture_m5_network.py check-approval \
  --plan operation-plan.json --approval <separately-approved-document>
python3 tests/target/capture_m5_network.py replay --series <raw-bundle-index.json> \
  --source <12-digit-image-source> --abi <symbolic-errno-values.json>
```

`plan` emits `{plan, approval_template}` with all physical flags false. A completed approval document is not present in this preparation. The gated `capture` command is reserved for a later explicitly approved operation; its required arguments identify the plan, approval, all three manifests, current guard session, boot number and new evidence directory. It is not included in software/CI scripts. Programming and normal restoration remain separate accepted M3 operations and require their own fresh safety verification.

Each retained boot directory contains exact `uart-rx.bin`, `receive.jsonl`, `poll.jsonl`, `writes.jsonl`, `controls.jsonl`, `peer-stops.jsonl`, phase peer JSONL and operation/safety records. The raw bundle index supplies each boot's reset(operation, monotonic), nonce, `uart_directory`, `writes_file`, `controls_file`, `peer_stops_file` and `peer_files` keyed by phases 2–8. Paths are relative to the index. Operator receipts are only the two acknowledged cable operations, stamped with the shared `m3_clock` CLOCK_MONOTONIC helper. They describe actions actually performed under approval; writing a receipt never performs an action. Do not fabricate records or copy M3 physical records into the M5 bundle.

Operator annotation helper, used only after the separately approved physical action actually occurred:

```sh
PYTHONPATH=tests/target python3 -c 'from m5_capture_adapter import record_operator_receipt; record_operator_receipt("/absolute/current/boot-dir", "cable_disconnect_receipt", confirmed=True)'
```

Use `cable_connect_receipt` only after the separately approved reconnect. The helper records shared-clock acknowledgement, performs no hardware action and cannot replace native host/target witnesses. Passive UART before the approved fresh reset is retained with exact byte range and SHA-256; a reset cut and one fresh source banner delimit qualifying evidence. Unknown prefix activity or extra boots fail. A shared host-boot identity binds the nonrenewable series clock across all three captures.
