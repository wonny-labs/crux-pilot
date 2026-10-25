# PR #64 integration with PR #65 Linux naming

Parents: previously qualified PR #64 HEAD `74d2610` and accepted main `69e26c7`
(PR #65). The user authorized this final integration, private System Linux naming
alignment, affected software checks, independent review and final-HEAD CI. PR #64
remains Draft, Issue #30 Open; no PR merge or hardware operation is authorized.

## Scope and preservation

The normal main merge completed without content conflicts. PR #65's File,
Logging, Monotonic, Netif and Serial Linux source/header/private-target changes
and Monotonic port/integration test names are retained. System adds only:

- Byte-identical move from `ports/linux/system_host.c` to `system_linux.c`.
- Private target `crux_system_host` to `crux_system_linux`, updating its existing
  include/link/compile/property references and the existing public facade's link.
- Affected static-analysis source path.

Public `<crux/system.h>`, `crux::system`, provider/configuration values including
`CRUX_SYSTEM_PROVIDER=host`, error/state semantics and approved source/test
behaviour are unchanged. `tests/system/host.c`, `crux_system_host_test` and
`System.Host.*` retain Host as the development/test role. No new public Linux
composition alias, compatibility shim or filename-only regression test was added.
Existing executable consumer, runtime and guard tests qualify the build wiring;
content hashes establish the implementation is a pure rename.

Historical plans/logs/manifests keep their original source identity and filenames;
this record provides the current rename mapping. It does not retroactively claim
that prior CI validated this new integration.

## Focused verification

Use the documented Linux development environment and existing checks:

```sh
cmake --build build/linux/debug --target crux_system_common crux_system_host_test
ctest --test-dir build/linux/debug -R '^System\.' --output-on-failure
python3 -m unittest tests.build.test_external_application
python3 -m unittest tests.build.test_consumer_boundary.ConsumerBoundaryTest.test_system_provider_selection tests.build.test_consumer_boundary.ConsumerBoundaryTest.test_system_usage_rejects_private_leakage tests.build.test_native_interfaces
scripts/check-format.sh
scripts/check-dependencies.sh
```

Affected clang-tidy also uses the regenerated compilation database and renamed
Linux path. Worktree Git mapping is scoped to individual scripts only; temporary-
repository tests run without Git environment overrides. Focused logs normalize
checkout paths/trailing whitespace. No hardware or unrelated capability changes.
Full applicable Host/Zephyr PR and Push CI is verified on the final published HEAD
and recorded on PR #64; previous-head results do not count for this gate.

Physical watchdog/reset/timing acceptance remains outstanding. Previously recorded
process-exit delays remain unresolved; successful checks do not establish a cause
or fix. The earlier local Git-environment failure is historical and distinct.

Focused results: System Host 25/25; external application 11/11; System provider/
usage plus native-interface regression 20/20; affected clang-tidy, format and
dependency checks pass. The 33 content records in `preserved-system-content.tsv`
match the qualified System parent, including the mapped Linux filename. Accepted
main component contents are unchanged.
