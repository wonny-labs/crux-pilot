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

Expected functional outcomes are fixed: echo returns 0; silent peers return symbolic `ETIMEDOUT`; TCP early close returns `ECONNRESET` while its UDP partner returns 0. Link-loss calls may return `ETIMEDOUT`, `ENETDOWN`, `ENETUNREACH` or `ECONNRESET`. Other outcomes fail this proposal and require investigation rather than automatic criterion expansion. The actual compiler-derived errno map supplies numeric values.

The frozen input model retains west's clean manifest-checkout record as workspace bootstrap context, separately from the built worktree commit/snapshot. The target compiles explicit worktree/module paths and exactly four pinned dependency revisions; the manifest checkout's source is not substituted for the worktree. A mismatched input model was found during the first freeze attempt and corrected with actual five-record input tests before the final candidate was rebuilt.

## Historical candidate and independent review (superseded)

The following is retained historical software evidence, superseded by the integrated candidate identified below. Historical candidate source is `2f7e6486a1c3bf67a341b9e97bf0c0575bbd2076`.
The subsequent PR documentation/evidence commit does not change executable inputs.
This identifies a cross-built candidate for later approval, not a physically qualified image.

| Artifact | Bytes | SHA-256 |
|---|---:|---|
| BIN | 97,376 | `e27f65e37107d40d0018135c13a6fbc8c2ccb7518ca6b1f68e1229bf3d5b0913` |
| ELF | 2,211,560 | `97a4cc502d1e3f8aa00f3424d79b00b7b804124d9d7058df1043c96187ee8ba8` |
| Configuration | 43,230 | `f2c969e39807202febc936b3eb8c41fd4a4928e85aaf8c7ecbf0e44b7afda3e1` |

Private image manifest SHA-256 is `a32a955550658ee1acfe612466b81b5c2e6db4aaeb10c580fe6321de7c1764d7`,
under `build/evidence/m5-preparation/frozen-2f7e6486a1c3/image-manifest.json`.
The [public evidence index](evidence/m5-physical-preparation.json) records every image/configuration/provenance role,
public log hash, retained private original hash, and normalization/excerpt rule.
Private originals contain absolute paths and are retained locally; they are not published.
The 367-file clean source snapshot has SHA-256
`fd2cb1204e59c80722fd547550dc41fbacbc58d4d8bee2258919210340b9ba69`.

Executed Linux checks: Host debug 71/71 and ASan/UBSan 71/71; build tooling 253/253
(including M5 70/70); existing embedded-libc Zephyr networking 3/3; private native
fixture 1/1; RT1060 reference and dedicated candidate cross-builds; format,
scoped dependency checks and configured Host analysis. The private C analyzer
reported no diagnostics. Configured clang-tidy suppressed non-user warnings.
The private native ztest was invoked directly; no Twister report is claimed.
Native idle snapshots returned contexts 0, packets 16/16 and buffers 32/32;
its test-thread/observer unused observations were 4056/2008 bytes. These are
native software observations, not physical target stack results.

Candidate linker use is FLASH 97,376, RAM 34,332 and DTCM 13,936 bytes. The ELF
contains 4160/2112-byte guarded main/observer allocations for usable 4096/2048-byte
stacks. All 51 frozen artifacts and 28 tools, complete eight-artifact build
provenance, exact ELF/BIN agreement, disabled writable storage and compiler/header
bound errno values were independently verified. Pinned errno values include
ETIMEDOUT 116, ENETDOWN 115, ENETUNREACH 114 and ECONNRESET 104.

An independent Reviewer and a separate Challenger both issued **PASS** at the
candidate source after inspecting actual source, contracts/boundaries, tests,
procedure and frozen evidence. Both independently ran manifest verification and
70 M5 tests. Findings corrected before PASS included record bool/phase/receipt
ordering, in-flight loss and UDP-down proof, peer reap, complete capture adapter,
unknown NIC rejection, failure cleanup, post-guard deadlines, canonical approval
permissions, pre-reset identity/current-image verification, passive raw prefix
separation, host-boot clock origin and workspace/source provenance distinction.
Neither review executed hardware or independently repeated all cross-builds.
Their PASS is confined to software preparation and the proposed procedure.

Excluded acceptance remains TCP servers, unconnected UDP, IPv6/DNS, multiple
interface management, runtime address changes, UDP in-flight physical loss,
concurrent/sustained-load capacity, active native-pool peaks and worst-case timing.
Issue #28 remains open. Actual entry/recovery manifests and artifacts, fresh
per-operation approvals, physical three-boot results and accepted normal
restoration/full verification/behavior are still required before physical acceptance.

## Independent review follow-up and integrated candidate policy

The external review of PR HEAD `a0e0e11` identified two confirmed tooling gaps.
The adapter now verifies actual direct-route output at initial acquisition and
reconnection: exact numeric lookup, interface, destination/mask, UP flags and
absence of a next-hop gateway, rejecting reject/blackhole/unknown forms. It
accepts the isolated connected `/24` or an on-link host route. Source address is
extracted from a fresh, validated dedicated-interface observation; it is not
presented as a source address obtained from `route get`. Route fields and their
source basis are retained with the original inventory output. This is an
observation at a named checkpoint, not socket binding or a route lock.

The narrow output model follows Apple's [route implementation](https://github.com/apple-oss-distributions/network_cmds/blob/main/route.tproj/route.c)
and [route manual](https://github.com/apple-oss-distributions/network_cmds/blob/main/route.tproj/route.8):
`print_getmsg` labels destination, mask, interface and flags, and prints a gateway
for a gateway route. Tests use synthetic network/host-route output and injected
processes; no actual interface/route command was run during preparation. Unknown
output fails closed and requires investigation under a separately approved trial.

The full canonical plan, including `procedure` and `evidence`, must match current
tooling. Approval additionally binds `plan_sha256`, the SHA-256 of the complete
plan serialized as sorted-key compact JSON. Operating notes belong in a separate
file and cannot override the approved plan. Old generated plans/approval
proposals must be regenerated; no physical approval has been granted here.

Replay PASS is explicitly labelled `communication_sequence_only`, with
`physical_acceptance=false`. Final physical acceptance separately requires the
actual approvals, guard/ownership, programming/entry verification, per-boot
preflight/reset records and normal restoration/full verification/behavior.
Automating that broader acceptance audit is outside this slice.

Latest main `5206909` was integrated, including `eaed2a0` Execution and native-free
consumer boundaries; both Execution and M5 CI/test/artifact configuration are
retained. The current candidate was regenerated from the integrated source with
corrected tooling, rather than reusing the old `2f7e648` image. Historical artifacts remain unchanged. All timing/resource thresholds,
client-only scope and physical operation gates remain as proposed; a late manual
disconnect remains INCONCLUSIVE rather than extending the functional deadline.


## Integrated candidate — current approval proposal

Candidate source is `22c2815154837322129f16124d5dc7bad8d2f682`, including main
`5206909e0d8d54df2d690226b06b3eb44c5c71e9`. Subsequent publication changes only
documentation/evidence. CI rebuilds may embed the publication HEAD; they do not
replace these frozen approval artifacts. The old `2f7e648` candidate is superseded for this proposal;
its artifacts and public evidence remain historical records.

| Artifact | Bytes | SHA-256 |
|---|---:|---|
| BIN | 97,376 | `c8848b910e06a65333439b35a93bb1bfd3d59838305dde83d9adb09e383bf890` |
| ELF | 2,211,748 | `197b268bef5d18f8b0fd86d4c1019a8999b8e3a421520853fc46121ff84864db` |
| Configuration | 43,265 | `603b9c0936c711d2e431a3103c6ee86baeceab4c9a64fb828cd653872dbfc9e8` |

Private manifest SHA-256 is
`9bf5f014e76d89499e12ae93ecb1ea4769ddf2fd84368986121a9cab4069d47a`,
under `build/evidence/m5-preparation/frozen-22c281515483/image-manifest.json`.
The clean 496-file snapshot SHA-256 is
`b86233d663ef5b6a384a2fa9b43b1c1b935e8c49eeda83e2a3c06bd464eecd29`.
The [integrated public evidence index](evidence/m5-physical-integration.json)
identifies public bytes, retained private originals, full image roles and dependencies.

Linker use remains FLASH 97,376, RAM 34,332 and DTCM 13,936 bytes. Main and
observer usable stacks remain 4,096 and 2,048 bytes. The configuration differs
from the historical candidate only by explicit `CONFIG_CRUX_EXECUTION` disabled;
no production Flash layout or writable-storage setting changed. Existing public
components and networking consumers match integrated main unchanged.

Independent integration review caught a test-only omission: the native-interface
checker's temporary repository lacked the three physical consumer files. The
fixture now includes them; dependency enforcement is retained. The failure and
corrected result are preserved with the route, approval and replay TDD evidence.

Executed integrated-source checks: Linux Host debug 76/76 and ASan/UBSan 76/76;
Python tooling 292/292 without skips, including M5 76/76; qualified embedded-libc
Zephyr networking 3/3 and dedicated native fixture 1/1; RT1060 reference and
candidate cross-builds; portable Execution Host consumers 2/2, Zephyr two
configurations/15 cases and both RT1060 consumer builds. Format, scoped dependency
checks and configured Host analysis pass. The analysis log retains suppressed
non-user warnings; it is not a claim of warning-free third-party code.

Reviewer and separate Challenger both issue **PASS** for software preparation and
the proposed procedure at the exact frozen source. Each independently ran the
manifest verifier and checked all 51 artifacts/28 tools. They inspected actual
image bytes, provenance, stack allocations, disabled storage and tool identities;
M5 76/76 was independently repeated. The integration Reviewer also repeated all
13 native-interface tests after the fixture fix. They did not independently
repeat every Linux build or operate hardware. The full canonical plan binding,
fresh validated direct route and explicit sequence-only Replay result resolve
the external review findings. Native idle stack observations remain 4056/2008
bytes, not RT1060 measurements. All physical timing/resource criteria above are
unchanged proposals requiring separate approval.

Physical acceptance remains pending. Before execution, separately approve the
full canonical plan hash and exact manifest/ELF/BIN/configuration/tool identities,
resolve accepted entry and normal recovery artifacts, and grant each enumerated
inventory/probe, programming (including implicit reset/start), three per-boot
preflight/reset, cable disconnect/reconnect and final restoration operation.
Safety gates must remain valid at each operation. Reset-based recovery requires
new approval and cannot satisfy phase-8 recovery without reset. Replay PASS alone
cannot establish physical acceptance. Issue #28 remains Open; this preparation
performs no hardware operation, merge or Issue closure.
