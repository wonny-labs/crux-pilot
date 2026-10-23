# Native-free interface enforcement plan

**Goal:** Deliver Issue #24's bounded policy, automated negative controls,
independent C/C++ consumer and reviewed controller migration map.

**Baseline:** accepted main `933e974`; existing Time contract and pinned Zephyr/NXP.
The user authorised implementation, software verification, independent review and
Draft PR preparation. No new public capability, hardware, merge or Issue closure.

**Approach:** Extend existing dependency checks with a bounded lexical source
policy and actual CMake usage-interface traversal. A standalone consumer builds
against public targets, while controlled native providers retain native ownership.
Compiler-only checking misses legal but prohibited native dependencies; source-only
checking misses transitive build leakage. Use both without introducing an AST or
general dependency framework. Current native controller fixtures retain a migration
map instead of being rewritten ahead of their approved capability contracts.

- [x] Source gate: `scripts/check-native-interfaces.py`, existing dependency runner,
  `tests/build/test_native_interfaces.py`; watch negative controls RED then GREEN.
- [x] Build gate: CMake usage check, independent C/C++ fixture and negative graph
  controls; fix demonstrated Zephyr public-interface leakage while preserving
  native provider registration and backend conflict checks.
- [x] Policy/map: `docs/native-interface-policy.md`; distinguish ISO C facilities
  from unqualified POSIX and consumer contracts from implementation providers.
- [x] Verify focused/full build-tool tests, Host build/CTest and affected Zephyr
  native_sim/EVKB compositions. Retain identities, commands, failures and limits.
- [x] Independent reviewer inspects source, policy, migration map and real evidence;
  correct material findings, prepare a focused Draft PR and leave #24 open.

## Approved networking integration

Merge current main without changing the accepted networking contract. Register
its exact portable consumer inventory with the common gate; allow only the
qualified socket headers and monotonic deadline API there. Preserve the stricter
networking profile checker. Add RED/GREEN controls for scope isolation, forbidden
threading/native operations, and CMake facade/close-declaration integrity. Separate
netif public usage from native provider registration where STATIC LINK_ONLY leaks.
Run focused checks before full regression and independent review. No hardware,
PR merge, Issue closure, or new capability contract is authorised.
