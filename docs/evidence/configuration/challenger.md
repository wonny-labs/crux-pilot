# Independent M4 implementation Challenger

Decision: **PASS for the approved bounded software implementation and honest acceptance handoff.** Native and physical Issue #16 acceptance remains incomplete.

Reviewed File commit `3a52636a1aa159b9df2aa90fc195c850b3186b2a` in `native-free-interfaces` and the updated Configuration source snapshot on `feat/portable-configuration` in `flash-safety-acceptance` on 23 October 2026. Configuration remains under final integration; any further substantive changes require affected re-verification. No source edits, upstream edits or hardware operations were performed.

## Prior material finding resolved

The earlier controller-scenario stub has been replaced with an actual application-owned scenario and registered as `Configuration.ControllerScenario` in the Host CTest build. I inspected its use of the existing `controller_observe()` and `controller_schedule_advance()` core, missing defaults, explicit separate saves, rejected update, cycle-4 activation snapshot, 20 observations and reconstructed-object reload of saved values. The declared range is 1–100; the accepted 100 ms schedule is unchanged. Independently compiling and running the scenario with the actual portable controller and Time sources under ASan/UBSan passed.

`docs/configuration-validation.md` maps all ten criteria and leaves native error propagation, actual persistence/interruption and physical support evidence pending. It does not treat reconstructed in-memory objects as a physical reboot. Its partial AC6/8/9/10 dispositions are appropriate; full Issue #16 or M4 completion is not claimed. No outstanding material source finding remains within this Challenger scope.

## Reviewed strengths and scope

- File implements explicit partial/zero-progress outcomes, preserves transferred counts on error, retires failed-close handles, checks range/EOF, and handles separate truncation cleanup without retrying backend objects. Host root ownership, regular-file checks and process-lifetime root descriptor are explicit.
- Configuration preserves accepted values after I/O errors, validates before publication, revalidates fallback defaults and before save, and distinguishes malformed/missing records from transport errors. Canonical encoding avoids native layout dependencies.
- Capability-specific result enums are faithful to the approved contracts and preserve richer outcome domains. They intentionally differ numerically from `crux_status_t`; headers/docs correctly prohibit casts or comparisons across domains. No base-enum extension or generic conversion framework is needed.
- File's VFS mapping is accurately labelled a plan, not an implemented or tested Zephyr provider. Configuration native provider observability and all physical persistence/Flash gates remain unproven. No production driver or partition is adopted.

## Concurrency limitation — nonblocking for this Host qualification

File holds its C17 atomic spinlock across complete provider calls, including blocking I/O and sync. This can consume CPU under Host contention and can deadlock/starve a priority-scheduled target if a higher-priority waiter prevents the lock owner from running. The current documentation explicitly restricts the qualification to this Host profile, does not promise parallel I/O or bounded latency, and does not adopt a Zephyr File provider. This is therefore not a blocker for the present software qualification. Before a target provider is adopted, review synchronization/ownership and avoid carrying this spin-across-blocking-I/O implementation forward as an accepted target solution. No OSAL or new concurrency abstraction is requested now.

## Independent focused verification

Compiled the actual shared sources and private test providers using `cc -std=c17 -Wall -Wextra -Werror -Wpedantic -fsanitize=address,undefined` into a temporary directory, then ran:

- File deterministic contract harness: PASS (`File deterministic contract checks passed`).
- Configuration scalar conformance: PASS (`CONFIGURATION_PASS checks=147 failures=0`).
- Updated actual-controller Configuration scenario with existing controller/Time sources: PASS (`CONFIGURATION_CONTROLLER_PASS result=0`). The first manual compilation omitted the controller public include directory; adding that existing include path corrected the review command without source changes.

Final lint-delta re-review: **PASS**. The private `crux_config_record_info_t` groups record length/count without changing their meanings; typed fault options preserve injected outcomes; bounds-checked key copy uses `memcpy`; output-value initialization uses valid declared enum members. Public API and persistence/error semantics are unchanged. The added `mixed_key_cases` demonstrates one successful key save followed by a failed second save, preserved accepted state, and reconstructed objects observing the individually stored mixed values without claiming transaction consistency. Independently rebuilt the current scalar tests under C17 warnings-as-errors and ASan/UBSan: `CONFIGURATION_PASS checks=163 failures=0`. The earlier 147 count is historical and superseded by this final focused run.

The Host regular-filesystem provider tests, independent C++ consumers, complete Linux regression suite, boundary checks and Zephyr builds were not independently executed in this Challenger pass. Their final retained results require root coordination verification. The retained Settings source-wrapper probe remains explicitly limited to Host shims and modeled backend outcomes; I previously inspected its full script and report and found the scope faithful. The current documentation preserves those limits. Focused passing tests do not establish actual Settings/NVS behavior, physical persistence, power-loss recovery or Flash/XIP safety.
