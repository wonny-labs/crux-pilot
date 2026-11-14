# Architecture decision records

Status: **in use**. [ADR-0001](0001-application-boundary-and-api-stability.md), [ADR-0002](0002-platform-direction-zephyr-execution-crux-services.md) and [ADR-0003](0003-p9-result-services-keep-narrow-thread-sync.md) are accepted, and the convention below applies to all three.

A decision record holds one platform-wide decision that stays in force: its context, the options, their trade-offs, the recommendation and the owner's decision. A decision that belongs to one capability stays in that capability's contract or design document, as today. Examples are the decision tables in the [product composition guide](../product-composition.md) and the [File root binding](../file-composition-interface.md). Records under `docs/plans/` stay as they are. They are historical, and the documentation check excludes them.

| # | Title | Status |
| --- | --- | --- |
| [0001](0001-application-boundary-and-api-stability.md) | Application boundary and public API stability | Accepted, D10 phase 1 only. Amended by ADR-0002 |
| [0002](0002-platform-direction-zephyr-execution-crux-services.md) | Platform direction: Zephyr for execution, Crux for services | Accepted, 13 November 2026. Amended by ADR-0003 |
| [0003](0003-p9-result-services-keep-narrow-thread-sync.md) | P9 result: services keep a narrow Crux Thread and Sync | Accepted, 14 November 2026 |
| [0004](0004-product-composition-of-management-services.md) | How a product composes the Crux management services (proposal) | Accepted, 14 November 2026, as recommended (C8 deferred) |
| [0005](0005-native-sim-test-watchdog-for-system-consumers.md) | How a consumer composes the Crux System service on native_sim with a test watchdog (proposal) | Proposed |

## Convention

- One file per decision, `NNNN-short-title.md`, numbered in order and never reused.
- Each record has a status line: **Proposed**, **Accepted** (with the owner's words, their date and where they were given), **Amended by ADR-NNNN** (the body stays and the later record names what it changes), **Superseded by ADR-NNNN** or **Withdrawn**.
- An accepted record is not rewritten. A later decision supersedes it with a new record, and the old record's status line links to the new one.
- A record states options, trade-offs and a recommendation for each decision. It ends with a list of the owner's decisions.
- The facts behind a record, with their sources, can sit in a research note under [`docs/research/`](../research/app-boundary-and-api-stability.md).
- Accepting a record decides. It does not change code, checks or protected paths. Each of those is its own reviewed change.
