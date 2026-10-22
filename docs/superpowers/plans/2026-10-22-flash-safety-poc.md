# Isolated Flash safety PoC implementation plan

**Goal:** Experimentally correct source-confirmed false success, polling hangs and Flash-resident MEMC configuration references without modifying pinned upstream checkout or selecting production policy.

**Authority:** User approved autonomous bounded software-only experimental implementation and draft PR preparation. No hardware operations, production layout/backend adoption, merge or closure. Implementation stays opt-in.

**Approach:** Generate private copies of the exact pinned NOR, MEMC and HAL source files, using a checked deterministic transformation. Existing MDIO candidate source substitution is the repository precedent. Prefer this over a parallel Flash driver or dependency update. Use an iteration budget to bound polling attempts in the PoC; this is explicitly not an elapsed-time guarantee. Unknown-state XIP failures enter a RAM-resident nonreturning terminal stub; non-XIP callers receive errors. No recovery/reset policy is adopted.

**Review focus:** Later-command failures must not continue programming; both erase-sector loops; HAL FIFO/reset/command waits; no deadline helper or MEMC config in Flash; returning after error must never resume uncertain XIP. NMI/vector/startup/DMA safety remains an explicit physical-write blocker.

- [x] Add source-pinned generation and failing host cases for WREN/program/erase/status errors, stuck busy and late-operation faults. Compile actual generated function bodies with controlled native stubs; assert exact call order and no XIP return.
- [x] Implement checked error propagation and bounded polling experiments in private copies; keep default upstream source selection untouched. Test lower-layer actual wait bodies where feasible, not only mocked transfer calls.
- [x] Add an isolated compile-only Zephyr PoC sample selecting generated source copies, RAM config and ITCM terminal. Build on pinned baseline, inspect ELF load/symbol/disassembly and data references. No partition changes.
- [x] Run relevant host regressions and focused fault tests; record exact source/config/artifact identity and limitations. Independent Reviewer inspects final source and evidence; fix findings, prepare Draft PR with no hardware-safety claim.

Baseline: clean origin/main 94b11e4; prior investigation Host 42/42 and native reproduction 25 cases. Tests and production source-selection defaults must remain intact.
