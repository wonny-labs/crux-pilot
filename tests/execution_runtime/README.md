# Independent synthetic runtime consumer

This original C17 fixture models a small runtime task/session adapter. It is not
production runtime integration or evidence of compatibility with any proprietary
runtime. Its author used the approved execution contract, public declaration shapes
and public build integration instructions; provider source, private headers and
portable conformance tests were not consulted.

`crux_runtime_fixture_run()` is callable from a standalone runner or firmware app.
Only ISO C and `<crux/execution.h>` appear in the portable source. The fixture uses
its own task priorities and stack-word requests, translates these to public Crux
options, and adapts a runtime argument to the portable task entry. Host functional
priority acceptance is tested, not native scheduling equivalence.

Each of twelve sessions creates its own mutex and three bounded signal channels.
A worker first waits behind a gate, allowing its owner to check poll and finite
join timeout while retaining the task. The owner holds the shared-state mutex
while a handshake makes the worker check poll contention and finite lock expiry.
After another handshake, the worker changes the protected value and signals
progress. The owner joins before inspecting worker results and destroying objects.
Full/empty channels, token preservation and finite empty waits are also checked.
The timeout adapter distinguishes retry, deadline, capacity and other failure.

Four reservation cycles create gated tasks until capacity is rejected, then release
and join all of them. The fixture bounds its reservation array at eight entries and
expects the qualification profile's four thread slots. A composition with eight or
more slots requires an intentionally larger fixture bound. Subsequent sessions and
cycles verify reclamation, including after an unsuccessful create. Worker stack
requests are zero/default for reservations and 2048 bytes for session tasks; the
three priority classes are all exercised.

Checks print source lines and actual status values and remain active with NDEBUG.
On an error, cleanup releases the owner lock, permits cooperative progress and
attempts bounded joins. Objects still reachable by an unjoined worker are retained;
static argument/session storage preserves their lifetime. A failed call terminates
the qualification run: do not call the fixture again after failure. The fixture has
one caller and is not reentrant. This deliberately provides no cancellation policy.

The handshake determines unavailability; there is no public clock or native sleep.
These cases check timeout outcomes, not measured elapsed time or realtime latency.
The outer test deadline is only a liveness guard.

## Standalone Linux Host composition

Configure the consumer itself, supplying the Crux root explicitly. The consumer
turns off Crux internal testing and samples and obtains capability headers and
provider linkage solely from `crux::execution`:

```sh
cmake -S tests/execution_runtime -B build/runtime-consumer \
  -DCRUX_SOURCE_DIR="$PWD"
cmake --build build/runtime-consumer
ctest --test-dir build/runtime-consumer --output-on-failure
```

The Crux Host root selects the supported Host execution provider. No native library
or private include directory is added by the consumer. Sanitizer flags can be
supplied to this independently configured build, without changing its source.

## Independent Zephyr composition

The `zephyr/` application owns its own `prj.conf`, enables `CONFIG_CRUX_EXECUTION`
and `CONFIG_ZTEST`, and disables unrelated Time capability initialization. Its
Zephyr-only runner calls the fixture from one ztest. The portable fixture is built
as a separate strict C17 static library linking only the public usage target. Use
the configured west workspace with this Crux repository discovered as its module:

```sh
west build -b native_sim/native/64 tests/execution_runtime/zephyr \
  -d build/runtime-consumer-native --pristine
# Run the native executable under an external liveness deadline:
timeout 60s build/runtime-consumer-native/zephyr/zephyr.exe
west build -b 'mimxrt1060_evk@B//qspi' tests/execution_runtime/zephyr \
  -d build/runtime-consumer-evkb --pristine
```

If the active west manifest registers another Crux checkout, use the repository's
`scripts/test-portable-execution.sh`; it explicitly selects this module and the
pinned dependencies through `ZEPHYR_MODULES` to avoid duplicate discovery. The module
owns provider selection and resource configuration; this application does not
include its private build files or name private targets. The EVKB command is a
cross-build only. No flashing, reset, probe or hardware execution is included.

## Initial RED evidence

Before the public header was installed, a Linux-container C17 syntax probe of
`src/runtime_fixture.c` failed with:

```text
runtime_fixture.c:4:10: fatal error: crux/execution.h: No such file or directory
```

This establishes the missing public capability dependency, not behavioral failure
coverage. After the public header and Host target arrived, the independent Release
configuration succeeded and the fixture compiled warning-clean with C17, `-O3` and
`-DNDEBUG`. Linkage then failed at the expected unimplemented `crux_mutex_*` and
`crux_semaphore_*` symbols. The consumer compile command inherited only the public
execution/base include roots (plus its own fixture header directory). Full linkage
and runtime results must be recorded separately once synchronization providers are
available. No stub API was substituted.

## Host qualification result

After synchronization implementation became available, the independently configured
Release consumer rebuilt and linked successfully. `Execution.RuntimeConsumer` passed
under CTest (one test, zero failures), and a separate direct execution under a
60-second external deadline reported:

```text
runtime fixture: PASS (12 sessions, 4 bounded reservation cycles)
```

The Release configuration retains `-DNDEBUG`; fixture checks remain active. Logs
are retained locally under `build/evidence/m5-investigation/runtime-host-release-*`.
This records Linux Host behavior only; Zephyr and hardware evidence are separate.

## Independent Zephyr qualification result

The independently configured `native_sim/native/64` Twister scenario executed its
one ztest successfully: one configuration passed, one case passed, zero failures,
errors or warnings. The callable fixture ran the same twelve sessions and four
reservation cycles used on Host. The original run output is retained under
`build/evidence/m5-investigation/runtime-zephyr-independent-initial.log`; the
latest strict C17 rerun is in `runtime-zephyr-independent.log`. The portable
fixture compile command is retained in `runtime-zephyr-compile-command.json` in
the same evidence directory. It includes only its own fixture and public
execution/base header directories, with no native includes or requirements.

This is simulated Zephyr execution, not EVKB hardware evidence.
