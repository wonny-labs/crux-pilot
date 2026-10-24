# Serial static composition validation plan

**Goal:** Satisfy Issue #27's updated M5 build/configuration rejection criteria and provide a bounded handoff to Logging #29.

**Architecture:** Extend the existing Serial CMake and immutable Devicetree binding checks. Keep one dedicated UART, existing native configuration mechanisms and existing test runners; no sharing implementation, registry or M6 model.

**Requirements:** User-authorised continuation of PR #58; latest Issues #27/#29/#32. Preserve M4, planning PR #2, public Serial contract, physical gates and open/Draft status.

- [x] Add real build-negative cases to existing Python build checks, opt-in for the Zephyr environment and invoked by test-serial.sh. Demonstrate runtime-only owner checks fail this acceptance test before fixing them.
- [x] Add static rejection for missing/disabled bindings, unsupported Serial provider/resource selections, Console/Shell/logging/management owners and unsupported UART child consumers. Follow pinned Zephyr's actual selections and retain CSI rejection.
- [x] Convert the existing runtime logging-collision fixture to a positive dedicated-UART coexistence test; cover fallback and all selected logging-list entries with negative builds.
- [x] Run affected Host Serial/consumers, native_sim, build-boundary and negative checks using the supported Linux container. Reuse prior unrelated qualification evidence and inspect PR #57 without integrating it.
- [x] Obtain independent Reviewer and Challenger verdicts on the final diff and evidence; correct material findings and re-review.
- [x] Record acceptance mapping, tested results and Logging decisions; prepare the Draft PR #58 update for publication after verification. Do not merge, close issues or operate hardware.
