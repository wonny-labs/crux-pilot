# Independent messaging consumers

These synthetic C17/C++17 consumers were written from the approved messaging
contract and public Execution declarations without inspecting Queue/Monotonic
provider implementation. They now use typed Time observation and Thread sleep;
original design and verification history below describes its stated revisions. They establish representative source-level porting
evidence, not compatibility with a production runtime.

Configure independently on the supported Linux environment:

```sh
cmake -S tests/messaging_consumer -B build/messaging-consumer-agent -G Ninja
cmake --build build/messaging-consumer-agent
ctest --test-dir build/messaging-consumer-agent --output-on-failure
```

Portable C and C++ object/library targets consume only public usage targets and
run the actual `CruxConsumerBoundary.cmake` checks. The final executable
composition explicitly selects Host providers. No consumer makes native calls.

The runtime checks exact copied records with per-producer sequence/payload
integrity; empty/full polling and finite waits; two producers and one worker in
four alternating finite/continuous sessions; semaphore progress notification;
mutex-protected cooperative stop between queue operations, even after successful
traffic; unavailable full/empty cancellation paths; and join timeout followed by
release and successful join. Static argument storage and resources are retained
on unsuccessful join. A failure stops subsequent sessions before their static
storage could be reused. Finite waits make stop observable; no hard cancellation
latency, fairness or no-post-stop-commit promise is inferred. The unavailable-path
notification proves an earlier timeout, not admission of the next operation.

The separately built `samples/messaging/controller.c` demonstrates absolute
monotonic releases and a consumer-owned 100 ms phase/skip rule. It preserves the
historical controller fixtures and does not add callback timers or modify RTC
behaviour. Neither scenario qualifies physical target timing or stack margins.

## Initial RED observation

Before capability targets were wired, the configure command above was executed
in container `crux-m5-execution` with `/opt/crux-cache/venv/bin` on `PATH`, at
baseline `225cd21b5fff37c31c5dff52570e9f26179682a1` plus the approved contract/plan and
new consumer sources. It exited 1 with `unknown consumer target: crux::queue`
from the real `crux_assert_portable_usage` check. Build directory:
`build/messaging-consumer-agent`. This is missing-public-surface RED evidence,
not a runtime failure result.

## Host verification

The independent configure/build/CTest command above passed with Clang 18 on the
supported Linux container: `Messaging.RuntimeConsumer` and
`Messaging.ControllerConsumer`, 2/2 passed. CTest retains output under
`build/messaging-consumer-agent/Testing/Temporary/LastTest.log`.

During integration, the actual guard also rejected the existing Execution
facade with `native consumer requirement: crux_execution_host
INTERFACE_LINK_LIBRARIES: Threads::Threads`. The composition owner corrected the
empty Threads usage requirement for the supported libc-pthread environment;
these consumer checks were retained and then passed. No provider implementation
source was inspected to author or adapt the consumer expectations.

A separate Zephyr runtime fixture resides in `zephyr/`; its composition selects
the selected Queue, Time, Thread and Sync capabilities and compiles the same portable runtime source. Use an
explicit `ZEPHYR_MODULES` path when testing the isolated worktree, to avoid
also discovering the workspace's primary Crux checkout. Its execution status
must be read from the current test report; merely providing this fixture does
not establish native_sim or EVKB results.

An additional continuous-traffic regression run passed 10/10 executions:

```sh
ctest --test-dir build/messaging-consumer-agent -R Messaging.RuntimeConsumer --repeat until-fail:10 --output-on-failure
```

## Native composition investigation

The first explicit-module native_sim build passed portable usage/source-target
CMake guards and compiled the shared runtime, but failed its final image link:
`metadata` was multiply defined by Queue and existing Execution native providers.
No runtime result was inferred from this failed build. Logs/report were retained
in `build/messaging-consumer-agent-native-explicit`; the composition owner was
informed for correction. This is an integration finding from public consumers,
not an expectation adapted from provider implementation.

After the composition owner namespaced Queue's native mutex, the same combined
image rebuilt successfully and Twister `--test-only` passed 1/1 executed
native_sim configuration. The original failed report remains as
`twister.initial-link-failure.json`, and the original `build.log` was copied to
`build.initial-link-failure.log` in the scenario's build directory before retry.
The final report is `build/messaging-consumer-agent-native-explicit/twister.json`.
No physical board execution occurred.

The native invocation used the isolated worktree as `repo`, with the supported
workspace `/workspaces/crux` and environment `ZEPHYR_BASE=/workspaces/crux/zephyr`:

```sh
west twister -j 2 -p native_sim/native/64 -T "$repo/tests/messaging_consumer/zephyr" -O "$repo/build/messaging-consumer-agent-native-explicit" --inline-logs --extra-args="ZEPHYR_MODULES=$repo"
```

After the observed integration fix, `cmake --build` was run on that scenario's
existing build directory, followed by the identical Twister command with
`--test-only`. The earlier `EXTRA_MODULES` invocation intentionally remains a
failed setup observation: it discovered both primary/worktree modules and
reported duplicate `crux_base`; it is not a product-runtime test verdict.
