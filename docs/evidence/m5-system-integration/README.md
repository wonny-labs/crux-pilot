# PR #64 integration with the accepted external application model

Integration parents: qualified System branch `d0c081d` and accepted `main`
`7558fa9` (PR #57). The user approved a normal merge preserving both histories,
software checks and independent reviews, while explicitly keeping PR #64 Draft
and Issue #30 Open. This supersedes the earlier conditional merge approval.
No PR merge, hardware operation or unrelated functionality is authorized here.

## Resolution scope

- Root CMake retains main's optional component loop, external Base-only defaults,
  `CRUX_BUILD_TESTS` separation from parent `BUILD_TESTING`, sample requirements
  and deferred selected public-target checks. System joins that loop and its
  existing tests register only when internal tests and System are selected.
- Native-interface policy retains external native-composition rules and System's
  public/consumer boundary. No native provider exception or guard weakening.
- Test guide retains external/Logging guidance and adds System commands and limits.
- System's independent Host fixture now selects only `CRUX_BUILD_SYSTEM`, respects
  an external `CRUX_SOURCE_DIR`, uses sample paths from that source root and does
  not force parent test/sample settings. The shared consumer wiring retains the
  existing Zephyr-relative fallback when no external root is supplied.
- The existing external-application fixture adds a System-only C17/C++17 profile,
  rejects unselected System symbols, checks provider conflicts and exercises the
  copied independent System fixture within a parent with its own tests/languages.
  README/capability inventory documents this integration without new APIs.

## Qualified implementation versus integrated build changes

System public headers, common lifecycle, both native providers, Controller policy,
common/Host/native tests and independent consumer source remain byte-identical to
`d0c081d`. Their earlier conformance evidence remains historical qualification,
not an assertion that an old CI run validated this merge.

The accepted main Queue/Monotonic/Time source/build definitions, CMake usage-graph
checker and existing Serial/Logging consumer wiring are retained unchanged.
The lexical checker retains main inventory entries together with the previously
qualified System inventory entries; its checking logic is unchanged.
New verification focuses on the resolved root/consumer graph and uses complete
applicable Host/Zephyr workflows on the final published HEAD. Their exact run
links/results are recorded on PR #64 without another source-changing CI cycle.

## Reproduction and evidence

Use the documented Linux Dev Container for the focused commands:

```sh
python3 -m unittest tests.build.test_external_application
python3 -m unittest tests.build.test_consumer_boundary tests.build.test_native_interfaces
cmake --build build/linux/debug --target crux_system_common crux_system_host_test
ctest --test-dir build/linux/debug -R '^System\.' --output-on-failure
scripts/test-system.sh --host-only
scripts/check-format.sh
scripts/check-dependencies.sh
```

`external-red.txt` retains the actual copied System consumer failure before the
path/parent-policy correction. `system-profile-red.txt` retains the missing
System selection/usage-target failure before root registration. Logs normalize
checkout paths and trailing whitespace. Final focused results and independent
review records accompany this evidence; hosted CI is an external acceptance gate
against the published HEAD, not inferred from prior runs.

No hardware acceptance, production timing guarantee or process-exit root-cause
resolution is implied. Previously recorded process-exit delays remain unresolved.

## Focused results before hosted CI

- System Host: 25/25 passed; standalone independent consumer: 2/2 passed.
- External application module: 11 tests passed, including all existing profiles,
  new System-only runtime/provider rejection and copied consumer parent policy.
- Source/usage boundary modules: 46 tests completed, 45 passed and one environment-
  induced failure. The affected dependency entry-point negative creates a temporary
  Git repository. A container worktree Git override had been inherited by the
  entire test process, so its child Git commands referred to the source repository
  instead. The unintended common `core.worktree` override was removed and primary
  worktree path/clean status verified. The isolated test rerun with no override
  passed (native-token rejection and ISO-C acceptance); no production/guard logic
  was changed. Retained failure and targeted rerun distinguish this from a clean
  first run. Final hosted CI uses no such worktree overrides.
- Formatting and dependency checks are executed separately with the Git mapping
  scoped only to their command, not to temporary-repository test harnesses.

The environment mistake above is unrelated to previously unresolved process-exit
observations; it does not establish their root cause or resolution.

## Independent review

[Reviewer](reviewer.md) and [Challenger](challenger.md) returned PASS for the actual
normal merge commit `76485af44491d663067efd5eb6dbc678f38f366c`, its ordered parents
and completed focused evidence. No material findings remain. The two editorial
findings are corrected. This later review-record commit changes documentation
only; complete final-HEAD hosted CI remains a separate acceptance gate recorded
on PR #64. The PR remains Draft and Issue #30 remains Open.
