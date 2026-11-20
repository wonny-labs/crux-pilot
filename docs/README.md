# Crux documentation index

This page is the entry point to `docs/`. It tells a reader who has only this repository which pages to read first, which pages describe the current state and which are historical records, and what the project's recurring terms mean. It adds no decision and no claim beyond the pages it links.

Start at the [repository README](../README.md) for what Crux is and how the repository is laid out. Come back here to find the right page.

## Reading order by audience

### An application developer who uses Crux in a product

1. [Choosing an API](choosing-an-api.md): the developer rule that says where each kind of product code goes (Zephyr directly, a Crux service, or plain C in the product's own domain).
2. [Architecture overview](architecture.md): the three layers of a product, the component ownership rules and the capability inventory.
3. [Product Composition Guide](product-composition.md): how a product repository composes, versions and releases Crux. There is no Crux release or tag yet.
   If the product targets a board that this repository has not worked with, read [Porting Crux to a new board](porting-to-a-new-board.md) next: the test ladder, what to review, a checklist and a first-board-run outline.
   If the product has a part with no upstream Zephyr driver, or a driver of its own, read [Writing a product driver](writing-a-product-driver.md): the module layout, a binding, a driver, an emulator and its `native_sim` tests, and the hardware scenario that stays gated.
   If two or more products share boards or drivers, read [Shared boards and drivers across products](shared-hardware-modules.md): where each board and driver lives, and the organisation manifest and hardware module templates.
   If products compile vendor kits that arrive in deliveries on more than one line, read [Vendor kits across repositories](vendor-kits.md): kit integrity, a two-line worked example, the kit path rule and the release record. It is a recommended convention, Experimental.
4. [Build and Integration Guide](build.md): setup, builds and external applications.
   If the product writes Zephyr code of its own, read [Zephyr and driver code in a product repository](coding-guidelines.md#zephyr-and-driver-code-in-a-product-repository) for the rules and the advisory checks, and walk a driver through the [driver review checklist](driver-review-checklist.md). A [contribution guide template](../templates/product/CONTRIBUTING.md) for the product repository is part of the product template.
5. [Component readiness](component-readiness.md): per component, which environments and which evidence exist, and what is missing.
6. If the product needs remote management over HTTPS, the command line or SFTP: [Secure management in a product](management-in-a-product.md), then the contracts it links. To judge what is verified in that stack before a product relies on it, read the [secure management review guide](secure-management-review.md).
7. If the product must start only signed images and take firmware updates: the [boot overview](boot-overview.md), then the [secure boot and update review](secure-boot-and-update-review.md). Nothing in the boot path has run on a board yet.
8. [Testing a consumer application](build.md#testing-a-consumer-application) and the [test guide](../tests/README.md).

### Migrating an existing controller product to Crux

For a team that moves a controller runtime (for example a PLC runtime on an i.MX RT board with eMMC storage and SPI I/O cards) onto Crux and Zephyr. Read in this order:

1. [Choosing an API](choosing-an-api.md): Zephyr directly for threads, locks, queues, timers and buses; Crux for platform services; plain C for the product's own behaviour. GPIO, I2C and SPI are frozen and to be removed, so bus I/O such as an SPI backplane uses Zephyr's SPI API in an adapter.
2. [Product Composition Guide](product-composition.md) and the [product template](../templates/product/README.md): repository layout, west manifest, versions and release.
3. [Porting Crux to a new board](porting-to-a-new-board.md), then [Writing a product driver](writing-a-product-driver.md) for a part with no upstream driver, and [Shared boards and drivers across products](shared-hardware-modules.md) if several products share boards or drivers. [Vendor kits across repositories](vendor-kits.md) applies if the product compiles vendor kits.
4. Storage: [File composition interface](file-composition-interface.md), [File contract](file-contract.md) and [eMMC and Reliance Edge composition](emmc-reliance-composition.md). The eMMC pages record simulator and link-check evidence only; no eMMC hardware result exists.
5. [Component readiness](component-readiness.md) for what each component has been tested with, and the [test guide](../tests/README.md) for the checks to run.

### Security and CRA work (secure boot, update, SBOM, static analysis, vulnerabilities)

1. Boot and update: the [boot overview](boot-overview.md), the [secure boot and update review](secure-boot-and-update-review.md), [boot composition](boot-composition.md) and [taking the composition to an RT1062 product board](boot-porting-rt1062.md) (its eMMC variant is section 4). Nothing in the boot path has run on a board.
2. Update state: the [image state contract](image-state-contract.md) (Experimental code, tested with a Host test double only). The [verification ladder](secure-boot-verification-ladder.md) says which level of test can prove which claim.
3. SBOM and releases: the [SBOM section](product-composition.md#sbom) and [upgrades and security fixes](product-composition.md#upgrades-and-security-fixes) of the product composition guide, the [product release record](../templates/product/RELEASE.md), the [compatibility table](compatibility-table.md) and the [release bundle checker](release-bundle-checker.md) (the last two are provisional).
4. Dependencies and vulnerabilities: the [dependency review procedure](dependency-review.md) and its records for [Mbed TLS](dependency-review-mbed-tls-4.1.1.md), [the wolf projects](dependency-review-wolf-pins.md) and [FatFs](dependency-review-fatfs-r0.16.md), with [ADR-0006](decisions/0006-cryptographic-dependency-policy.md) for the cryptographic dependency policy.
5. Static analysis and coding rules: [static-analysis coverage](static-analysis-coverage.md), [static analysis of target code](coding-guidelines.md#static-analysis-of-target-code) and the [MISRA C position](coding-guidelines.md#misra-c-position) in the coding guidelines.

Known gap: the repository has no security policy file (`SECURITY.md`) and no recorded route for reporting a vulnerability. Raise it with the maintainer before relying on one.

### A platform contributor

1. [CONTRIBUTING.md](../CONTRIBUTING.md): code ownership, public interfaces, Git and pull request conventions, review requirements.
2. [Architecture overview](architecture.md), [Composition interface rules](composition-interfaces.md) and [Coding and Public API Documentation Guidelines](coding-guidelines.md).
3. The [decision records](decisions/README.md) for the platform-wide decisions in force, and the capability contract of the component you change (the `*-contract.md` pages listed under [Contracts and designs](#contracts-and-designs)).
4. [Build and Integration Guide](build.md) and the [test guide](../tests/README.md) for the commands and the checks that apply.
5. For secure boot, firmware update or the management services, the two board test procedures: [secure boot](secure-boot-board-test-procedure.md) and [secure management](secure-management-board-test-procedure.md). Neither has been run on a board, and neither approves a board operation. The [secure boot bench session kit](secure-boot-bench-session.md) orders the first secure boot board work into one session with a consolidated approval checklist; it is a proposal and has not been run either.
6. For boot work, the developer pages beside the boot overview: [taking the boot composition to an RT1062 product board](boot-porting-rt1062.md), the [boot image inspection](boot-image-inspection.md) tool and the [boot reproducibility check](boot-reproducibility.md). The two tools are developer tools, not Crux interfaces, and the reproducibility check is not wired into CI and has never run on a real build. For HAB, the [HAB status page](boot-hab-status.md) says what exists and what does not (no board result and no signature verification exists). To plan which level can prove which secure boot claim, read the [verification ladder](secure-boot-verification-ladder.md); to bring up a product board, use the [product board hand-off checklist](secure-boot-product-board-handoff.md). Neither reports a result on a board.
7. [AGENTS.md](../AGENTS.md) if an AI agent works on the change (see the [glossary](#concepts-glossary)).

### A reviewer or manager

1. The [README](../README.md), especially its status and limits paragraph.
2. The [decision records](decisions/README.md): what has been decided and by whom.
3. [Component readiness](component-readiness.md): the state of each component, with the evidence level behind it.
4. The two review packages: the [secure boot and update review](secure-boot-and-update-review.md) (start with the [boot overview](boot-overview.md)) and the [secure management review guide](secure-management-review.md). Each says what exists, what was verified and by which means, and what is still open.
   The boot pages that follow the review are guidance and tools: the [RT1062 porting guide](boot-porting-rt1062.md), the [boot image inspection](boot-image-inspection.md) and the [boot reproducibility check](boot-reproducibility.md). Each opens with a status block that says what was verified and what was not; none reports a boot on a board.
5. The [GitHub milestones](https://github.com/wonny-labs/crux-pilot/milestones) and [issues](https://github.com/wonny-labs/crux-pilot/issues) for delivery status. Documents in this repository describe contracts and recorded evidence, not the current schedule.

## Who to ask

In these documents "the owner" is the repository maintainer: the person who approves platform decisions and merges pull requests. A team that works on its own copy names its own maintainer for that role.

Open a [GitHub issue](https://github.com/wonny-labs/crux-pilot/issues) in `wonny-labs/crux-pilot`. A missing composition interface, a documentation gap and a question about a decision all go there. The repository records no other contact channel.

## Current and historical pages

Most pages in `docs/` describe the current implementation or a current decision. A page that is a historical record says so at its top or is named below. The pages that carry a dated status line (for example the boot review pages, which say "current on" a date) are current guidance as of that date, not timeless statements; the code and CI at your revision are authoritative. Do not read a historical page as the present state of the project.

### Historical records

| Page or folder | What it is |
| --- | --- |
| [`implementation-status.md`](implementation-status.md) | Acceptance and execution status of the Time Pilot milestones M0 to M5. Last evidence update 2026-10-17. Not the current platform status |
| [`baseline.md`](baseline.md) | Frozen build and physical evidence of the original Time Pilot, with exact commands and results |
| [`m2-physical-verification.md`](m2-physical-verification.md) | Plan and authorisation record for the first physical Flash, reset and UART verification of the reference controller's execution (early platform milestone M2) |
| [`m3-validation.md`](m3-validation.md) | Closed execution and hardware-validation record of early platform milestone M3 |
| [`m5-pilot-validation-plan.md`](m5-pilot-validation-plan.md) | Validation and completion plan of early platform milestone M5 (issues #25 to #30) |
| [`execution-profile.md`](execution-profile.md) | Investigation that selected the reference controller's execution model |
| [`physical-verification-plan.md`](physical-verification-plan.md) | Unexecuted plan, never an approval, for physical checks of GPIO, I2C and SPI. Component readiness still cites its open items |
| [`flash-safety-poc.md`](flash-safety-poc.md) | Isolated proof of concept for Flash safety. Not an adopted driver or layout |
| [`storage-pilot-software-handoff.md`](storage-pilot-software-handoff.md) | Storage handoff of the Pilot, superseded as the selected storage direction |
| Boot investigation records | The boot, eMMC and bootloader-update investigations ([boot and update](boot-update-investigation.md), [eMMC](boot-emmc-investigation.md), [bootloader update](boot-bootloader-update-investigation.md)) are desk studies that record what a study found when it was written. Current guidance is in the [boot overview](boot-overview.md) and [boot composition](boot-composition.md) |
| `plans/` | Plans, designs and proposals. Many are completed or superseded, some are still active, so read each file's own status line |
| `history/` | Superseded proposals and snapshots |
| `evidence/` | Frozen validation records and review artefacts. They are kept as recorded and are not guidance |
| `superpowers/plans/` | Execution plans of earlier work, kept as development records |

The documentation link check does not read the folders `plans/`, `history/`, `evidence/` and `superpowers/plans/`, or `baseline.md`, `implementation-status.md` and `m3-validation.md`. Links from a maintained page into them are still checked.

### Current pages

Everything else in `docs/` is maintained. The groups are:

- **Orientation and guides:** [architecture](architecture.md), [choosing an API](choosing-an-api.md), [build and integration](build.md), [product composition](product-composition.md), [porting to a new board](porting-to-a-new-board.md), [writing a product driver](writing-a-product-driver.md), [vendor kits across repositories](vendor-kits.md), [driver review checklist](driver-review-checklist.md), [component readiness](component-readiness.md), [composition interface rules](composition-interfaces.md), [native-free consumer boundary](native-interface-policy.md), [coding guidelines](coding-guidelines.md).
- **Decisions:** [decision records](decisions/README.md) and the [research notes](research/platform-direction-review.md) behind them.
- **Contracts and designs:** see below.
- **Investigations, feasibility reports and physical gates:** the boot pages (start with the [boot overview](boot-overview.md) and the [secure boot and update review](secure-boot-and-update-review.md); then [boot composition](boot-composition.md), [boot and update](boot-update-investigation.md), [eMMC boot](boot-emmc-investigation.md), [bootloader update](boot-bootloader-update-investigation.md), and the developer pages [RT1062 porting](boot-porting-rt1062.md), [image inspection](boot-image-inspection.md) and [reproducibility](boot-reproducibility.md)), the eMMC and microSD pages, the `rt1060-*` pages, the [secure management review guide](secure-management-review.md), [RT1170-EVK feasibility](rt1170-evk-a1-feasibility.md) (a software-only record; the RT1170-EVK is set aside for now, see the [boot overview](boot-overview.md)) and the dependency reviews. They record what a study found and which of its options were taken; read each page's own status line.

## Contracts and designs

A contract page defines one component's public behaviour, its limits and its validation. A validation page records the tests and results that back it.

| Area | Pages |
| --- | --- |
| Time | [Time contract](time-contract.md), [CPU Busy-wait](time-busy-wait.md) |
| Execution and messaging | [Thread and Sync](thread-sync.md), [event and queue](messaging-contract.md), [validation](messaging-validation.md) |
| Communication and networking | [Serial](serial-contract.md), [serial validation](serial-validation.md), [networking](networking-contract.md) |
| Storage and configuration | [File](file-contract.md), [Configuration](configuration-contract.md), [File root binding](file-composition-interface.md) |
| Diagnostics and system | [Logging](logging-contract.md), [System health and reset](system-validation.md) |
| Device I/O (frozen, to be removed: use Zephyr's APIs, see [Choosing an API](choosing-an-api.md#frozen-then-removed-gpio-i2c-and-spi)) | [GPIO](gpio-contract.md), [I2C](i2c-contract.md), [SPI](spi-contract.md) |
| Boot and update | [Image state contract](image-state-contract.md) (Experimental; Host test double only) |
| Secure management | The [glossary](management-glossary.md) first, then the [access](access-contract.md), [credential store](credential-store.md), [web](web-contract.md), [command line](cli-contract.md) and [SSH provider](ssh-provider-contract.md) contracts. The [README](../README.md#secure-management) has the full table |

### Approved contracts under `superpowers/`

The folder `docs/superpowers/` is named after a development tool that produced its first files. It holds approved designs and plans that other pages link by path, so the files stay where they are. Four design documents are current contracts, and the plans beside them are development records:

- [Time redesign](superpowers/specs/2026-10-25-time-redesign.md)
- [Logging design](superpowers/specs/2026-10-24-logging-design.md)
- [Serial design](superpowers/specs/2026-10-24-serial-design.md)
- [System health design](superpowers/specs/2026-10-24-system-health-design.md)

## Milestones

Two numbering schemes exist, and they are separate:

- **Pilot milestones M0 to M5** belong to the original Time Pilot plan. They are historical. Their acceptance identifiers, such as `M5-03`, appear in `baseline.md`, `implementation-status.md` and the Pilot plans. They do not describe the current platform.
- **Platform milestones** are the GitHub milestones of this repository, for example M4 for storage and configuration services, M8 for the boot platform and M11 for HTTPS web management. They reuse some numbers of the Pilot milestones. The [milestone list](https://github.com/wonny-labs/crux-pilot/milestones) is the source for their scope and status.

When a page says "M2", "M3" or "M5" without more, check its date and its neighbours: a page that cites `docs/baseline.md` or a Pilot plan means the Pilot milestone, and a page that cites issue numbers (for example the pages named above for issues #6 to #12 and #25 to #30) means the early platform milestone of that number, not the Pilot's.

## Stability tiers

[ADR-0001 Decision 4](decisions/0001-application-boundary-and-api-stability.md#decision-4-api-stability-and-change-policy) defines four tiers for public interfaces:

| Tier | Meaning |
| --- | --- |
| Experimental | Being tried. It may lack a Zephyr composition outside `tests/`, and may change in any release |
| Unstable | An adopted contract with a consumer and tests. It may change in a minor release, with a release note and a migration note |
| Stable | Covered by a compatibility promise. Only from `v1.0` |
| Deprecated | Still works, with a compiler warning and a named replacement |

Nothing is Stable, because no `v1.0` exists. The tier of each component in [Component readiness](component-readiness.md) is provisional: ADR-0001's assignment of a tier to every public interface (its follow-up change 5) is not done, and only some public headers, those of the management services, Configuration, the web and command line composition interfaces, the Reliance Edge file system type and the image-state contract, carry a `Status: Experimental` line. Separately, `thread`, `sync`, `queue` and `serial` are **frozen for new product code**, and `gpio`, `i2c` and `spi` are frozen as Experimental and to be removed (ADR-0001 owner decision D4). A freeze is not a tier; see [Choosing an API](choosing-an-api.md).

## Decisions

Platform-wide decisions are recorded as ADRs in [`decisions/`](decisions/README.md). ADR-0001 to ADR-0006 are accepted. The titles of ADR-0004 to ADR-0006 still carry the word "proposal", which is a leftover of their drafting: the index lists all six as accepted.

## Concepts glossary

| Term | Meaning |
| --- | --- |
| **Portable component** | A Crux component, under `components/`, written in C17 and independent of Zephyr, board and vendor headers. OS-specific code usually sits in its `ports/` directory |
| **Public header** | A header under a component's `include/crux/`, included as `<crux/NAME.h>`. It is the interface a product uses |
| **Usage target** | The CMake target a consumer links to use a component, such as `crux::time`. A usage target is not an installed package or a binary SDK |
| **Composition** (platform composition) | The board- or project-specific configuration and initialisation that choose OS implementations and bind hardware resources, such as Kconfig, providers and device bindings. A **composition interface** is the one formal way composition code reaches a provider that must be bound to Zephyr resources ([rules](composition-interfaces.md)) |
| **Provider** | The implementation behind a Crux interface for one environment, for example a Linux Host provider or a Zephyr provider |
| **Zephyr-direct** | Product code that uses Zephyr's own APIs for OS and hardware facilities, such as threads, locks, queues, devices and buses, instead of going through a Crux wrapper. The [developer rule](choosing-an-api.md) makes this the default for those facilities |
| **Frozen for new product code** | The status of `thread`, `sync`, `queue` and `serial`: no new features, existing users keep working, and any deprecation waits for a separate decision. Their public headers can print an opt-in compiler warning (`CRUX_WARN_FROZEN_HEADERS`), as can those of `gpio`, `i2c` and `spi`, which are frozen differently: ADR-0001 decision D4 freezes them as Experimental and plans their removal after the first tag |
| **Decision codes** (P3, R4, O4, D10, I5, Q2 ...) | A letter and a number name a row in one decision record or proposal. The letter depends on the document. **P1 to P10** are the platform decisions of [ADR-0002](decisions/0002-platform-direction-zephyr-execution-crux-services.md). **R1 to R6** of ADR-0003 are the results of [ADR-0003](decisions/0003-p9-result-services-keep-narrow-thread-sync.md). **D1 to D10** are the owner decisions at the end of [ADR-0001](decisions/0001-application-boundary-and-api-stability.md#owner-decisions) (other pages, such as the secure boot development plan, have their own D rows: read the document that is linked beside the code). **I1 to I9** and **Q1 to Q9** are the decisions and open questions of the [image state contract proposal](plans/image-state-contract-proposal.md). **Q1 to Q8** in ADR-0006 are its own questions. **O1 to O5** (with O4a) are the options of the [platform direction review](research/platform-direction-review.md#4-options): O1 is ADR-0001 as it is, O2 Zephyr only, O3 a full portable layer, O4a ADR-0001 with a real-time carve-out, O4 Zephyr for execution and Crux for services, O5 a documented POSIX subset. ADR-0003 later reuses O2 and O4 for the internals of the services. **C1 to C9** are the choices of ADR-0004 and **S1 to S9** those of ADR-0005. **R1 to R8** and **O1 to O8** are also used for case identifiers inside the [image state contract](image-state-contract.md); those are local to that page. A code that is local to one document is defined in that document. Follow the link next to a code to find its row; a bare code does not decide anything by itself |
| **D4** (of ADR-0001) | The owner decision to freeze the I2C, SPI and GPIO APIs now as Experimental, announce deprecation at the first tag and remove them in the next tagged release. Not to be confused with "Decision 4" of the same ADR, which is the API stability policy |
| **Build A and Build B** (ADR-0001, D10) | The two builds of the phase 1 experiment in ADR-0001: Build A uses plain Zephyr, Build B uses Crux with an adapter. They exist to compare the two ways of building the same product |
| **EVKB** | The NXP i.MX RT1060 evaluation kit, revision B (`mimxrt1060_evk@B//qspi`), the first board of the project. It has QSPI NOR and a microSD slot and no eMMC |
| **The Pilot** | The original Time Pilot, the first phase of this repository. Its milestones M0 to M5 are historical ([Milestones](#milestones)). "The platform" means Crux as it is developed now |
| **west groups `crux-boot`, `crux-reliance`, `crux-secure-management`** | Optional groups of projects in `west.yml`, all off by default: MCUboot (`crux-boot`); the Reliance Edge file system, GPLv2 (`crux-reliance`); and the TLS, PSA and SSH stacks of the management services, Mbed TLS, TF-PSA-Crypto, wolfSSL, wolfSSH and wolfPSA (`crux-secure-management`, off by default so that an ordinary workspace fetches no GPLv3 source) |
| **Mbed profile, Wolf profile** | The two sets of TLS and PSA libraries a composition can select: Mbed TLS with TF-PSA-Crypto, or wolfSSL with wolfPSA. SSH is available on the Wolf profile only. See the [management glossary](management-glossary.md) and [Secure management in a product](management-in-a-product.md) |
| **Bench image** | The management image built for the RT1060 EVKB board (`tests/zephyr/management_rt1060_compile`), planned in [the bench image plan](rt1060-bench-image-plan.md). Not a product image |
| **WP1a** | The first work package of milestone M8 (boot platform), tasks 0 to 7 of issue #296. See the [boot overview](boot-overview.md#2-the-stages) |
| **Sysbuild** | Zephyr's way to build several images, here MCUboot and the application, in one build |
| **RAM load, trailer** | The MCUboot mode that copies the signed application to RAM and starts it there. The trailer is the state area at the end of an image slot (magic, `image_ok`, `copy_done`) that MCUboot and the image-state contract read and write |
| **HAB** | The i.MX RT boot ROM's check of the first-stage image. See the [HAB status page](boot-hab-status.md) |
| **Host** | The Linux development and test composition on a developer machine or in CI. It is not an Embedded Linux product variant |
| **`native_sim`** | Zephyr's simulated board, which runs a Zephyr image as a process on the Host. A `native_sim` result does not prove behaviour on a board |
| **Evidence levels** | E1 Host test, E2 `native_sim`, E3 EVKB build, E4 physical board, as defined in [Component readiness](component-readiness.md#how-to-read-this). None implies another |
| **ADR** | Architecture decision record: one platform-wide decision with its context, options, trade-offs and the recorded decision ([index](decisions/README.md)) |
| **The owner** | The repository maintainer: the person who approves platform decisions and the merge of every pull request. A page that says a decision was "approved by the owner" cites that maintainer's approval. See [Who to ask](#who-to-ask) |
| **AGENTS.md and CLAUDE.md** | Instructions for AI agents that work in this repository: what they may do on their own, what needs the owner's approval, which paths are protected, and which checks to run. A human contributor reads [CONTRIBUTING.md](../CONTRIBUTING.md) |
| **Draft PR, reviewer PASS** | Substantive changes arrive as Draft pull requests with recorded validation evidence and an independent review. A review PASS does not authorise a merge |

## Status and limits

Crux is not production-qualified and is not an installable SDK. Its components have different readiness. No Crux release or tag exists. No physical-board evidence exists for the secure management services. The RT1170-EVK is set aside for now: its pages are software-only records, and the first board is the RT1060-EVK Rev B. Static analysis is clang-tidy with [`.clang-tidy`](../.clang-tidy), run by `scripts/analyze-host.sh` over an explicit list of paths and by other check scripts, most of them over shell globs; [static-analysis coverage](static-analysis-coverage.md) measures what that covers and proposes a way to cover every compiled source, a proposal that is not adopted. The [README](../README.md#status-and-limits) states the limits in full.
