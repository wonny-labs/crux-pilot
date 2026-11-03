# Portable access control contract implementation plan

> **For agentic workers:** executed natively by one agent, because every task shares `components/access/`
> and its private state. A fresh `reviewer` agent checks the whole branch at the end.

**Goal:** Implement the accepted access control contract (Issue #177) for the Linux Host with fake
providers, so that gate #183 can review it.

**Architecture:** A portable core in `components/access/src` reaches the outside world only through the
private ports in `access_port.h`, and uses `crux::sync` for its two locks. Fakes live in `tests/access`
and exist only when Crux builds its own tests, so no product can select them.

**Tech stack:** C17, CMake, CTest with standalone C tests. No new dependencies.

**Spec:** [docs/plans/access-contract-proposal.md](../../plans/access-contract-proposal.md)

## Global constraints

- Host only. No PSA, store or Zephyr provider, no manifest change, no hardware operation.
- Existing protected paths stay unchanged, including `.github/`, `scripts/` files that exist and
  `cmake/CruxConsumerBoundary.cmake`. New check scripts may be added.
- A conflict with the proposal is recorded and raised, not silently resolved in the contract.
- Limits, from the proposal: names 1 to 31 characters of `[a-z0-9_.-]`, passwords up to 128 bytes, reference
  rule 12 to 128 bytes, 8 sessions with one reserved for serial, 3 sessions per account, 8 accounts, 2 keys per
  account, 32 source and 32 name buckets, 5 free attempts, base 1 s, cap 60 s, quiet period 15 minutes.

## Review focus

1. A login racing a change: disable, password change, removal and recreation during verification.
2. Discovery through any observable: results, iteration counts, table-filling, events.
3. Concurrent administrators leaving no enabled administrator.
4. Time at the 64-bit limit and a clock that never advances between sessions.
5. A store that claims `VALID` while holding a malformed record.

## File structure

| Path | Responsibility |
| --- | --- |
| `components/access/include/crux/access.h` | Public contract |
| `components/access/adapter/crux_access_adapter.h` | Adapter contract |
| `components/access/src/access_port.h` | Private ports and compile-time bounds |
| `components/access/src/access_internal.h` | Private state shared by the core's source files |
| `components/access/src/access.c` | Init, policy validation, permission check, session queries |
| `components/access/src/access_session.c` | Session table |
| `components/access/src/access_delay.c` | Delay buckets |
| `components/access/src/access_record.c` | Account table encoding and validation |
| `components/access/src/access_login.c` | Password and key login, touch, logout |
| `components/access/src/access_accounts.c` | Change sequence, account operations, provisioning |
| `tests/access/` | Fakes and the conformance, security, fault and secret-absence suites |
| `tests/access_consumer/` | Independent consumer and boundary probes |
| `cmake/CruxAccessBoundary.cmake` | Rule that the adapter include directory cannot reach a consumer |
| `scripts/test-access.sh`, `scripts/check-access-boundary.sh` | New runner and boundary check |
| `docs/access-contract.md` | Implemented behaviour, choices the proposal left open, support limits |

## Tasks

Each task writes failing tests first, runs them under the `host-debug` build to see the failure, implements,
re-runs, then commits.

### Task 1: Scaffold, policy and fakes

- Create the headers, targets, fake ports and test helper. Add `access` to the root component list, and require
  `CRUX_BUILD_SYNC`.
- `crux_access_init` validates the whole policy, draws the hash key and loads the store.
- Tests: every invalid policy rejected, every operation `NOT_READY` before a successful init, random failure,
  repeated init, `EMPTY`, `VALID`, `UNUSABLE` and malformed `VALID` records.

### Task 2: Provisioning, sessions and permission checks

- Provisioning, password login without delay, `crux_access_check`, session queries, touch and logout.
- Tests: the conformance suite against two policies: permitted, unauthenticated, unpermitted, expired idle and
  absolute, stale handle, reused slot, generation wrap, provisioning refusals.

### Task 3: Delay state

- Buckets, windows, decay, serial bucket, source and name hashing.
- Tests: windows, cap, decay, guessing bound over 24 hours for several attacker strategies, collisions, flooding,
  serial independence, success clearing the name bucket only.

### Task 4: Equal treatment and races in login

- Dummy record, disabled and role-less accounts, verification slots, session reservation, re-validation,
  cost upgrade.
- Tests: equal results and iteration counts, table-filling probe, busy result, serial during a network
  verification, disable and password change during verification, removal and recreation, upgrade save failure.

### Task 5: Account operations

- Change lock, save-then-apply, last administrator, session ending rules, forced change, key operations.
- Tests: each operation for success, unauthenticated, unpermitted and invalid input, save failure leaving state
  unchanged, power loss, login during a save, concurrent last-administrator changes, a check that does not wait on
  a blocked save.

### Task 6: Key login

- `crux_access_key_is_authorised`, `crux_access_login_verified`, `crux_access_report_key_failure`.
- Tests: signed and unsigned attempts, windows, failed signatures, no name delay, forced change refusal.

### Task 7: Secret absence, faults and boundary

- Secret absence over a flow that uses every path; 64-bit time; lock failure; concurrent logins.
- `cmake/CruxAccessBoundary.cmake`, consumer probes, `scripts/check-access-boundary.sh`, `scripts/test-access.sh`.

### Task 8: Documentation, verification and review

- `docs/access-contract.md`, `tests/README.md`.
- All checks in the Dev Container image, the `reviewer` agent, Draft PR, removal of build output.
