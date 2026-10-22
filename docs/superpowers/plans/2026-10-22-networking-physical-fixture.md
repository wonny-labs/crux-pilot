# Networking Physical Fixture Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans inline with TDD and one independent whole-branch review. User has authorised execution of the preceding physical-test plan within software scope; no additional routine plan approval is required.

**Goal:** Prepare an identified RT1060 test-only composition and bounded capture/peer/oracle runner, without operating hardware.

**Architecture:** Keep public netif/socket contracts and samples/networking/client.[ch] unchanged. Private fixture control selects fixed-peer exchanges, primary-link observation and native resource/stack measurements. Reuse existing M3 clock, UART ownership, mount-guard, J-Link and isolated-route safeguards; do not reuse its incompatible application protocol/oracle.

**Tech Stack:** C17, Zephyr 4.4.0/SDK1.0.1, Python unittest, existing pinned tools.

**Spec:** User-approved minimum physical plan in this chat: three fresh boots; ten healthy TCP/UDP pairs each; silent peers, TCP early close, real loss and explicit fresh-socket recovery; 2000ms functional operation budget; whole-call latency measured independently (proposed2500ms review threshold,not a contract guarantee); external10000ms per-call host liveness stop;15000ms link bound;5000ms cleanup; contexts/connections4,packet16/16,buffer32/32;4096-byte main/1024 minimum unused. Native observer uses2048bytes/512 minimum unused. All are short-test acceptance objectives, not worst-case guarantees. Final hardware input/action approval remains separate.

## Global Constraints

- No hardware, probe, UART, programmer, reset, network-interface changes or physical fault actions during preparation.
- No public/common consumer changes; no new socket/clock framework,driver correction,capacity increase or automatic recovery.
- Fixed isolated board192.168.77.2/24,peer192.168.77.1,TCP42420/UDP42421; Host SIGPIPE policy explicit in peer composition.
- Freeze source/configuration/binary/ABI fingerprints; private raw vs normalized public evidence remain distinct.
- Main-independent observer samples public netif every50ms; target link-DOWN timestamp must overlap an actual call interval for in-flight-loss acceptance. Missed human timing is an INCONCLUSIVE trial, never a deadline extension.
- Final capture runner requires separate exact-image hardware approval, fresh guard/identity/ownership/route checks; no execution during this task.

## Review Focus

- Stale/duplicate/malformed commands or boot records must never produce traffic or acceptance.
- Timeout/failed cleanup/lost transport must stop finite series, without retrying descriptors or unapproved target changes.
- Resource/stack snapshots must describe target data and all owned fixture threads, not simulated target guarantees.
- Both-end32-byte correlation, actual carrier event and same-epoch fresh-socket recovery must be demonstrated; no reset substituted for in-place recovery.
- Tampered ELF/BIN/config/ABI/approval must fail before device access.

### Task1: fixture control and strict replay oracle

Files: tests/networking_physical/control.[ch],tests/target/m5_network_oracle.py,tests/build/test_m5_control.py,tests/build/test_m5_oracle.py,tests/build/test_m5_series.py.
Interfaces: fixture_control(line,session,command) accepts one ARM nonce then increasing sequence commands; validate_capture(rows,peer,manifest) rejects incomplete/reordered evidence.
- [x] Write behavioral command/oracle negative cases; observe RED; implement minimal state/validation; GREEN.
- [x] Include wrong nonce/sequence/count,late completion,missing resource baseline,missed in-flight DOWN,wrong peer frames and reset during recovery.

### Task2: target composition and software-only build checks

Files: tests/networking_physical/{CMakeLists.txt,prj.conf,main.c}; scripts/test-networking-physical.sh; related native tests.
Interfaces: private M5N1 framed consumer using only the approved standard profile; existing networking_exchange unchanged. Records begin/end,netif,resources,stack under one private record lock. Console waits for bounded explicit commands; no boot traffic.
- [x] RED core execution tests,then private native integration; never edit common consumer.
- [ ] Build native fixture checks and RT1060 with existing PHY overlay/MDIO correction. Run format,boundaries,relevant Host/build regressions.

### Task3: peer,capture and immutable image preparation

Files: tests/target/{m5_network_peer.py,capture_m5_network.py,prepare_m5_network.py}; focused build tests.
Interfaces: fixed32-byte peer protocol; replayable raw capture; exact-image manifest and explicit hardware-approval gate before device operations.
- [x] RED tests for peer framing/short-I/O/lifetime,approval before side effects,stale boot,deadline,resource and image tampering; implement GREEN.
- [x] Reuse existing safety helpers; distinguish HardwareStop from functional FAIL and preserve all failed evidence. No automatic programming/reset on unknown state.

### Task4: evidence and independent review

Files: docs/networking-physical-preparation.md and a compact public evidence index.
- [ ] Commit software inputs; build clean-source exact images and record ELF/BIN/config/ABI/tool hashes.
- [ ] Independent fresh reviewer inspects actual source,tests,raw software results,images and hardware gate; resolve material findings and repeat affected checks.
- [ ] Present final numeric criteria,exact image hashes,unsupported behavior and separate hardware-approval checklist. No hardware execution,merge,Issue closure or physical PASS.

## Ledger

Start: clean isolated worktree on feat/m5-networking-physical-fixture from current main df52a62. Prior M3 physical permissions are historical and not reused. Implementation is split across private fixture, bounded capture/oracle and immutable image tooling, with independent Challenger and Reviewer assessments. Plan tasks share record/schema and manifest identities; implement/test these once before capture integration.

Ruling after user refinement: use a private M5N1 framed test consumer so nonce/phase/protocol/transaction correlate on the wire without changing the approved fixed-payload consumer. Do not interpret functional2000ms as a2100ms whole-call guarantee. Independent Challenger reviewing exact criteria.


Final procedure/schema refinements for implementation:
- M5N1 frame32bytes: magic4 ASCII M5N1,nonce32BE,phase16BE,protocol16BE(1TCP/2UDP),transaction32BE(1..count),payload16 `crux-m5-echo-v1\0`. Version encoded in magic, no unspecified fields. Nonzero nonce differs for each of3freshboots. Duplicate/stale/malformed frames rejected. This physically exercises the private M5 consumer, not the unchanged reference sample/runtimeadapter.
- Capture entered,functional-completion-before-close,andreturned target CLOCK_MONOTONIC stamps. Functionalbudget2000ms; observedwholecall>2500ms=>REVIEW_REQUIRED,not a contract violation or automaticPASS; independenthost10000ms fromBEGINreceipt=>ABORT/INCONCLUSIVE,no cancellation/automatictargetchange. Settling5000ms starts after return.
- Perboot matrix: ARM; I1 baseline; P2 echo10; P3 silent1; P4 echo1; P5 TCPearlyclose/UDPecho1; P6 echo1; L7 losssilent1; P8 resetlessecho1 afterconfirmedrestoration; I9 final.16pairs/32calls perboot;48pairs/96calls across3boots. No reset withinthese9phases. Separate approvedresetrecovery is a distinctnonqualifying outcome if resetlesscasefails.
- Native resourcechecks aftereachcall:3full idle snapshots >=1000ms apart within5000ms afterreturn. Main4096/unused1024;observer2048/unused512. Percommand150000ms,whole1800000ms inclboundedoperatorwaits90s andsafetyreserve. No interactive timeextension.
- Oneoutstandingcommand and fresh readiness ticket aftercompletion/drain; queued/replayed commands cannot triggerlatertraffic. Noautoapptrafficatboot orfailure.
