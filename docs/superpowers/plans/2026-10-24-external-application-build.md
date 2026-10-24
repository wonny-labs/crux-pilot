# External application build implementation plan

**Goal:** Execute the user-approved Goal 02 from the attached specification: source-tree
`add_subdirectory()` consumption, explicit component/native composition, parent isolation,
representative external execution, preserved Host/Zephyr boundaries and a reviewed Draft PR.

**Spec:** User attachment `f8291b4c-4124-494d-8d2b-3acf6f79e219/pasted-text-1.txt`.
**Baseline:** `98f05104ad2bc915e4da93ab9e8732e850c140c5`.

## Constraints and baseline

No C API/runtime changes, package/install support, provider framework, new platforms,
physical operations, main push or merge. Keep existing public usage names, Thread/Sync
selection, Sockets declaration/sysroot exceptions and current qualification limits.

Linux external baseline reproductions (logs under ignored `build/evidence/goal02`):
plain inclusion enables Crux samples, GoogleTest and unrelated component targets;
fixture-style suppression sets the parent's BUILD_TESTING OFF but still adds unrelated
components. A Queue consumer linking only its public target fails with undefined symbols.
Both configurations otherwise configure successfully using the existing cached GoogleTest.

## Decisions and alternatives

1. Component-specific options default ON for top-level development, OFF for dependency
   use; Base is always available. Preserve CRUX_BUILD_THREAD/SYNC, add corresponding
   options for Time, Queue, Monotonic, Netif, Sockets, Configuration and File.
2. CRUX_BUILD_TESTS defaults to top-level BUILD_TESTING, OFF as dependency. Include CTest
   only for the top-level project; explicitly enable testing if Crux tests are selected.
   Samples default OFF as dependency. Never FORCE parent BUILD_TESTING or language settings.
   Internal full-suite/sample dependencies produce clear configuration errors if disabled.
3. Preserve established per-component implementations. Expose only Queue and Monotonic's
   existing Linux Host providers via documented composition-only aliases
   crux::queue_host and crux::monotonic_host. Portable targets continue linking
   crux::queue/crux::monotonic; native executable composition links the provider aliases.
   Thread/Sync/File public Host linkage stays unchanged. No generic composition helper.
4. CRUX_TIME_PROVIDER=memory must be explicit for external Time use, with a clear development
   only message; top-level development retains memory default. Reject missing, unsupported
   or multi-valued selections. Preserve Time's compatible-interface provider conflict guard.
   Missing Queue/Monotonic composition may fail at link time; do not add a resolver for this.
5. Compare alternatives: automatically adding Queue providers to public facades changes
   native dependency propagation; a generic binding helper adds machinery for two cases.
   Documented composition aliases preserve the existing boundary with the least new code.

## Task 1 — Root isolation and component/native selection

Files: root CMakeLists.txt, components/time/CMakeLists.txt,
components/queue/CMakeLists.txt, components/monotonic/CMakeLists.txt;
new tests/build/test_external_application.py and tests/external_application/.

- First add real external fixtures/tests (copied to temporary directories outside Crux),
  run on the baseline and retain expected RED output before production edits.
- Separate root development/subproject options, guard component additions and public checks.
- Add explicit Time selection checks and two composition aliases; preserve existing providers.
- Prove Base/Time, Thread-only, Sync-only, Queue/Monotonic and parent tests ON cases.
  Check actual compiled sources, final ELF symbols, public source commands, no GoogleTest,
  no unrelated capabilities, parent cache/language/test continuity and missing/bad/conflicting
  provider failures. Reuse the existing portable consumer guard and negative fixtures.
- Commit after focused external checks are green.

## Task 2 — Existing consumers, documentation and final validation

Files: relevant tests/*consumer/CMakeLists.txt, tests/execution_{firmware,runtime}/CMakeLists.txt,
tests/build/test_consumer_boundary.py, README.md, docs/architecture.md, tests/README.md;
existing CI only if needed to invoke the maintained new test.

- Migrate affected root consumers to component options instead of FORCE suppression;
  migrate Thread-only/Sync-only through the supported root entry point.
- Keep native fixture runners separate from checked portable source targets; do not remove
  necessary native fixture includes or alter test algorithms just for consistency.
- Document one tested add_subdirectory example, component defaults, native aliases,
  explicit development Time provider, supported Linux environment and exclusions.
  Full docs/build.md remains separate. Preserve current component contract semantics.
- Run affected consumer suites and existing Python/native-boundary negatives during changes.
- Once stable, run Host Debug/Sanitizer, representative native_sim/EVKB builds and existing
  quality checks. Run complete existing Host/Zephyr CI on final reviewed PR HEAD;
  retain failures and rerun affected checks before repeating broad validation.
- Independent Reviewer and Challenger: one substantive plan assessment and final actual
  diff/evidence assessment, with corrections and affected re-review for material findings.
- Submit one coherent Draft PR with actual local/CI results, review conclusions and limits.
  Do not merge; do not mark Goal complete until every specification gate is verified.
