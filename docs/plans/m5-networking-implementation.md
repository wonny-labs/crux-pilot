# Portable networking implementation plan

> Execute inline with superpowers:executing-plans and TDD; independent whole-branch review before Draft PR.

Goal: qualify the approved IPv4 TCP client/connected UDP standard profile and
read-only primary-interface observation for independent firmware/runtime consumers.

Architecture: libc/Zephyr provide standard sockets. A small netif component owns
its public snapshot and selects private Host/Zephyr observation providers. Portable
sample logic implements bounded framing and deadlines, without a socket wrapper.

Tech stack: existing C17/C++17, CMake, GoogleTest, Python unittest, Zephyr 4.4.0.
Spec: [approved proposal](m5-networking-proposal.md), qualified by the user's six
approval conditions in this chat on 22 October 2026.

## Constraints and review focus

- Preserve main baseline, M3 fixtures, existing Time/base and native dependencies.
- Client-only, single primary observation; no route/binding/address-management API.
- Host composition explicitly suppresses SIGPIPE; reusable libraries do not.
- Monotonic standard clock only for networking deadlines; no execution framework.
- Test repeated EINTR/EAGAIN against the same overall deadline, including expiry.
- Test TCP EOF, short I/O and send after closure; distinguish zero-length UDP.
- Test netif address independently of admin/carrier and unchanged output on error.
- Test numeric descriptor close exactly once on error and resource return separately.
- Test forbidden source/include/private linkage and independent C/C++ consumption.

## Task 1: read-only netif contract and providers

Files: components/netif/{include/crux/netif.h,src/netif.c,src/netif_port.h,
host/netif_host.c,zephyr/netif_zephyr.c,CMakeLists.txt}; tests/networking/netif_test.cpp.
Interface: crux_netif_get_status(crux_netif_status_t *out), existing crux_status_t;
private observation seam and shared IPv4 eligibility predicate.

- [ ] Write snapshot/error/eligibility tests; run them RED with a minimal stub.
- [ ] Implement caller-owned output and eligibility filtering, then GREEN.
- [ ] Add real Linux interface and Zephyr selected-interface providers. Bind Host
  name using composition build setting, Zephyr default interface at observation.
- [ ] Test native provider behaviour with controlled fixtures and real loopback.
- [ ] Commit contract/provider work with actual test evidence.

## Task 2: portable client/profile and independent runtime fixture

Files: components/sockets/CMakeLists.txt; samples/networking/{client.c,client.h,
host/main.c,zephyr/main.c,zephyr/CMakeLists.txt,zephyr/prj.conf};
tests/networking/{client_test.cpp,stack_test.cpp,runtime_port.c,runtime_port.h}.
Interfaces: crux::sockets standard usage target; sample-owned bounded exchange,
private test-only syscall/clock replacement; runtime fixture independently uses
standard profile and netif public contract.

- [ ] Write scripted real-client tests for partial I/O, retry/timeout/error/EOF,
  UDP length and close uncertainty; run RED before client implementation.
- [ ] Implement only sample framing/deadline policy; run GREEN.
- [ ] Add real Host loopback fixtures for TCP/UDP, silence, refusal, closure and
  fresh reconnect with descriptor counts before/after repeated attempts.
- [ ] Add runtime-shaped independent source and C/C++ consumer compile fixtures.
- [ ] Host composition installs SIGPIPE policy explicitly and tests post-close send.
- [ ] Commit passing client/profile tests.

## Task 3: Zephyr integration, boundaries and reproducible commands

Files: tests/networking_zephyr/{src/main.c,prj.conf,CMakeLists.txt};
scripts/{test-networking.sh,check-networking-dependencies.py};
tests/build/test_networking_dependencies.py; root/module CMake/Kconfig and CI.
Interfaces: same public targets and portable source on both compositions; native
loopback peer/control and pool observation remain private to integration fixtures.

- [ ] Write negative dependency cases RED, implement narrowly scoped checker GREEN.
- [ ] Run unchanged legacy dependency checks plus new source and CMake checks;
  verify actual independent consumer include/link requirements.
- [ ] Run real Zephyr loopback success/silence/close/refusal/reconnect/cleanup;
  observe carrier down/up through private controls and snapshot assertions.
- [ ] Cross-build RT1060 portable controller communication composition; no hardware.
- [ ] Integrate reproducible software-only command and hosted checks.
- [ ] Run Host debug/sanitizers, build tests, format/analysis/boundaries, applicable
  M3 network/integrated and Time/controller regression; retain complete logs.
- [ ] Commit implementation/tests and record exact source/config/resource limits.

## Task 4: independent review and delivery

Files: docs/networking-contract.md, docs/evidence/m5-networking-software.md,
tests/README.md and relevant README links.

- [ ] Audit every approved requirement against actual source/results; document
  unsupported features, routing assumptions, resources and pending physical AC.
- [ ] Independent Reviewer reads full diff, contracts, tests and raw evidence;
  correct material findings with RED/GREEN tests and obtain re-review.
- [ ] Commit reviewed evidence, push the dedicated branch, create/attach Draft PR.
  Do not merge, close issues or claim M5/physical completion.

## Execution ledger

22 October 2026: user approved the bounded architecture and authorised routine
implementation without another plan approval. Execute inline; delegate only the
required independent final review. Existing worktree is isolated and clean at
039932d. Baseline 42 Host tests and header spikes already passed; do not relabel
them as full profile evidence. Any material deviation returns to human review.

Implementation ledger: netif contract tests RED (5 failures) then GREEN;
Host provider test RED (observation stub) then GREEN; sample client tests RED
(14 failures) then GREEN. Host debug now 66/66 plus the existing sample scenario.
Boundary negative controls RED (21 missed controls), GREEN, then native-type
controls RED (3 misses) and GREEN. Independent C/C++ consumers build/run with
only public include paths and crux::base requirements. Host real stack passes
three repeated cycles and post-close EPIPE, with identical descriptor counts.

Ruling: native_sim external libc resolves standard socket calls to Linux rather
than Zephyr even when POSIX options are enabled. Require explicit Picolibc for
this fixture and verify native context allocation. Fetch only Zephyr's existing
pinned Picolibc revision as a test dependency; no API/architecture/toolchain
upgrade. Preserve the failed SIGPIPE run and correct the early probe's claim.

Ruling: CRUX_SOCKETS requires explicit native POSIX options instead of selecting
them recursively; Kconfig diagnosed a cycle through NET_IP/IPv6/POSIX. No broad
POSIX_API selection or global portable-check relaxation is introduced.
