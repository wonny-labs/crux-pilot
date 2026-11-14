# ADR-0004: How a product composes the Crux management services (proposal)

Status: **Proposed**, 14 November 2026. Nothing here is adopted, and adoption is the owner's alone. Baseline: `origin/main` at `a72032b2f29c8c4579de8de07a43b40f19bb9516`. Author: Claude Code, model `claude-sonnet-5-5`. The owner approved writing this proposal, and only the proposal, in the Crux Pilot main session on 14 November 2026 (relayed by that session as the words "추천대로 승인할게"). The scope for the product comes from the owner's decision in the Crux Controller chat on 14 November 2026 at about 16:30 UTC (also relayed): the management set plus the System service, HTTPS first on the Mbed profile, SSH and SFTP later on a manual job that publishes nothing. Excluded: Device I/O, the four frozen headers on the product surface, Reliance and eMMC, Linux and RT1170.

This record is documentation only. It adds no code, check, target, header or Kconfig symbol, adopts no public interface and changes no protected path. It does not edit an accepted ADR. Every change that it puts forward would be its own reviewed change, listed under [Follow-up changes](#follow-up-changes).

**Labels.** **Verified** means I read it at the baseline revision and cite `path:line`. **Reported** means a named document or note says it and I did not re-check it. **Inference** is my reading. **Not verified** means no run or read exists. No build, `native_sim` run, `west` command or test ran for this record: see [What was run](#what-was-run).

## The recommendation in brief

- **The gap is real.** At the baseline, a product that keeps to public `<crux/NAME.h>` headers and public `crux::` targets cannot compose HTTPS, the services, Access or SSH. Seven calls that a start sequence needs sit in private headers, and the Zephyr module builds none of the services' cores. The only full composition is a `native_sim` test image that compiles Crux's own source files into its application.
- **Recommended route: A**, composition interfaces under the [six rules](../composition-interfaces.md), phased, with a native_sim consumer fixture outside the repository as the boundary test. The Crux Controller session recommends the same. **Alternative: B**, one Kconfig-selected Crux composition. B is not covered by the six rules and would need an ADR-0001 amendment first, which is the main reason against it.
- **Publish less than the private headers hold.** The credential adapter header carries raw record read, write, key replace and erase. The Access adapter carries first-account provisioning. Publishing those headers whole would hand product code the means to mint an administrator or replace a device key. The proposal publishes a subset and keeps the rest private.
- **The first account decides the phase order.** Provisioning is allowed on the serial channel only, and the serial console is composed in no image. An HTTPS-first image with no serial setup step has no way to get its first account, so phase 1 needs the serial setup step before any board image. See [Phasing](#phasing-and-the-licence-boundary).
- **Licence boundary.** Keep wolfSSL, wolfSSH and wolfPSA out of a public product with manifest narrowing, a Kconfig default of off, and a test that fails if they appear. Narrowing is **Reported** to work by the Controller session, with `west` 1.4.0 and `west list`. I did not re-run it.
- **Nine decisions** for the owner are at the end, each with a recommendation.

## Context

[ADR-0001](0001-application-boundary-and-api-stability.md) (D3) adopted the [composition interface rules](../composition-interfaces.md). [ADR-0002](0002-platform-direction-zephyr-execution-crux-services.md) and [ADR-0003](0003-p9-result-services-keep-narrow-thread-sync.md) decided that product code uses the OS natively and that the services keep a narrow Crux Thread and Sync inside. The Controller session, which builds a separate public product, recorded the finding [management has no public composition](https://github.com/wonny-labs/crux-controller/blob/main/docs/findings/management-has-no-public-composition.md) (read with `gh api`, **Reported**). It asks what Crux would offer so that a product can compose the management features through public interfaces only. Open issues that touch this: #197 (one integrated image with HTTPS, CLI and SFTP), #185 (HTTPS on the Wolf and Mbed profiles), #190 (the TLS certificate model), #316 (SSH user key types) and #318 (how a product registers an SSH user key). All five are open (**Verified** with `gh issue view`).

## 1. Current state

### 1.1 What the Zephyr module builds

| Fact | Evidence | Status |
| --- | --- | --- |
| The module builds `cli` under `CONFIG_CRUX_CLI` and `web` under `CONFIG_CRUX_WEB`, among others. It adds no subdirectory for Access, the credential store, security events, the Configuration service, status, network settings, the TLS certificate module, the file service or reboot | `zephyr/CMakeLists.txt:82-86`, `:152-156`, and no matching `add_subdirectory` anywhere in the file | Verified |
| The `crux::` aliases that a Zephyr application can link are `base cli configuration file file_zephyr gpio i2c logging netif queue serial sockets spi sync system thread time web`. None of the services above is among them | `add_library(crux::… ALIAS` in `zephyr/CMakeLists.txt` and `components/*/ports/zephyr/CMakeLists.txt`, collected with the same pattern as `public_targets()` in `tests/build/test_product_template.py` | Verified |
| The Host build creates `crux::access`, `crux::credential_store`, `crux::tls_certificate` and the other service targets, which the Zephyr module does not | `CMakeLists.txt:28` (the component list) and `:121-130` (the loop that adds each enabled component) | Verified by reading, not run |
| SSH builds nothing in the module. `components/ssh/CMakeLists.txt` begins as private to Crux with no public header and no `crux::` target. A composition calls `crux_ssh_wolf_settings()` | `zephyr/CMakeLists.txt:158-175`, `components/ssh/CMakeLists.txt:1-2` | Verified |
| `crux::web` exists, and its public header `crux/web.h` declares route macros and request helpers, not the start | `components/web/ports/zephyr/CMakeLists.txt:28`, `components/web/include/crux/web.h` | Verified |

### 1.2 Which calls of the start sequence a product can reach

Sources: the test compositions `tests/native/management_composition/composition/crux_native_management.c` and `crux_native_management_services.c`, the boot order of [Secure management in a product](../management-in-a-product.md), and the declaring header of each call, found by `grep`.

| Call | Declared in | A product can include it |
| --- | --- | --- |
| `crux_security_event_init` | `components/security_event/include/crux/security_event.h` | Yes |
| `crux_access_init`, `crux_access_account_create`, `crux_access_account_add_key` | `components/access/include/crux/access.h:99,209,280` | Yes |
| `crux_network_settings_boot`, `crux_network_settings_start_worker` | `components/network_settings/include/crux/network_settings.h` | Yes, but they call a **private port** (below) |
| `crux_config_service_start`, `crux_mgmt_status_start`, `crux_file_service_start`, `crux_cli_init` | `components/config_service/include/crux/config_service.h:117`, `components/mgmt_status/include/crux/management_status.h:115`, `components/file_service/include/crux/file_service.h:101`, `components/cli/include/crux/cli.h:114` | Yes |
| `crux_reboot_start`, `crux_reboot_start_worker` | `components/reboot/include/crux/reboot.h` | Yes, with a public policy callback |
| `crux_credential_start`, `crux_credential_key_ensure` | `components/credential/adapter/crux_credential_adapter.h:95,170` | **No** |
| `crux_access_provision_first_account` | `components/access/adapter/crux_access_adapter.h:120` | **No** |
| `crux_tls_certificate_on_change`, `crux_tls_certificate_start`, `crux_tls_certificate_material_take` | `components/tls_certificate/adapter/crux_tls_certificate_adapter.h` (start at `:100`) | **No** |
| `crux_web_core_init`, `crux_web_start` | `components/web/adapter/crux_web_adapter.h:116,185` | **No** |
| `crux_web_session_init`, `crux_web_session_start_worker` | `components/web/session/crux_web_session.h:65,68` | **No** |
| `crux_web_management_init`, `crux_web_management_start_worker` | `components/web/management/crux_web_management.h` | **No** |
| `crux_network_settings_port_apply`, `crux_network_settings_port_running` | `components/network_settings/src/network_settings_port.h` | **No** (a port the product must implement) |
| SSH provider, attachment, host key | `components/ssh/src/crux_ssh_provider.h`, `components/ssh/ports/zephyr/crux_ssh_wolf_provider.h`, `components/ssh/adapter/crux_ssh_attachment.h` | **No** |

The definitions are where the brief said: `crux_web_start` at `components/web/ports/zephyr/web_http_server.c:458` and `crux_tls_certificate_start` at `components/tls_certificate/src/tls_certificate.c:335`. Only two headers sit under `include/crux/` in `components/web` and `components/tls_certificate` together: `web.h` and `tls_certificate.h` (**Verified**).

### 1.3 Why the guard does not catch the private headers today

`cmake/CruxConsumerBoundary.cmake:130` rejects a usage requirement that names `components/<c>/(src|ports|host|zephyr|tests)`. It does not name `adapter`, `management` or `session`. The Access and credential adapters have their own include-directory rules (`cmake/CruxAccessBoundary.cmake`, `cmake/CruxCredentialBoundary.cmake`). `crux_web_adapter` is an INTERFACE target with no `crux::` alias, marked in a comment as only for a composition (`components/web/ports/zephyr/CMakeLists.txt:31-35`). **Inference:** a consumer that adds `components/web/adapter` to its include path today is not rejected by `crux_assert_portable_consumer`. I did not run that case.

The product template's `templates/product/check_domain_includes.py` rejects the four frozen headers anywhere in `--product` mode, and rejects a composition interface `<crux/NAME_zephyr.h>` in the domain only. Its pattern `^crux/[A-Za-z0-9_]+_(zephyr|linux)\.h$` is already the rule 1 spelling (**Verified**).

### 1.4 The only full composition is a test

`tests/native/management_composition/composition/composition.cmake:6-9` says the components have no Zephyr module integration, so their portable sources are compiled into `app`. `:12-16` stops with an error unless the board is `native_sim`. The listed source files include `components/access/src/*.c`, `components/credential/src/*.c` and the PSA ports under `components/*/ports/psa/` (**Verified**). The RT1060-EVKB image `tests/zephyr/management_rt1060_compile` refuses to start and is a compile vehicle (**Reported** by [the feasibility report](../rt1060-management-image-feasibility.md)). Nothing in this area ran on a board (**Reported**, [Secure management in a product](../management-in-a-product.md)).

### 1.5 Sequence rules

| Rule | Evidence | Status |
| --- | --- | --- |
| `crux_access_init` returns before any other Access operation starts | `components/access/include/crux/access.h:6` | Verified |
| `crux_tls_certificate_start` is thread context only and returns before any other operation of the module starts | `components/tls_certificate/adapter/crux_tls_certificate_adapter.h:17` | Verified |
| `crux_web_management_init` runs after the services have started, before `crux_web_start`, and on one thread | `components/web/management/crux_web_management.h:48` | Verified |
| The material is registered with Zephyr's TLS credentials before the listener starts, and nothing is served if that fails | [TLS certificate](../tls-certificate.md), step 6 of the start table | Reported |
| Network settings boot runs before the Configuration service starts | [Secure management in a product](../management-in-a-product.md), Boot order | Reported |

All five are enforced by documentation and by the order in the test composition. I found no check that a product's own order is right (**Inference**, from the absence of one in `tests/build` and `zephyr/`).

### 1.6 What is verified and what is not

Verified: sections 1.1 to 1.5. Not verified: that any of the routes below can be built, that a consumer outside the repository can configure a Zephyr build against the module for `native_sim` with the services (the template builds only Time: `templates/product/app/CMakeLists.txt`), the serial CLI start API of `crux_cli_console`, and where SSH keeps its host key (two records differ, see hook 2 below).

## 2. Routes

**Route A, composition interfaces under the six rules.** Each capability that needs a Zephyr-bound start gets `crux::<capability>_zephyr` and `<crux/<capability>_zephyr.h>` under `components/<capability>/ports/zephyr/include/crux/`, an INTERFACE target that links the portable target, and a negative boundary test. The header publishes declarations that already exist (the File precedent, [File root binding](../file-composition-interface.md)). The product writes the start sequence in its own adapter code.

**Route B, a Kconfig-selected Crux composition.** Crux owns the start order in its module. `CONFIG_CRUX_MANAGEMENT` and sub-symbols select the services. A product calls one function, for example `crux_management_start(const crux_management_hooks_t *)`, and passes callbacks for what only it knows.

**Route C, the status quo.** The product copies Crux source files and uses private include paths, or Crux widens the boundary check. Rejected: the boundary check holds no exception by decision, and widening it would hide the finding.

| Question | A | B |
| --- | --- | --- |
| Covered by an adopted rule | Yes: the six rules of ADR-0001 D3 | No. `crux_management_start` orchestrates and adds behaviour, which rule 4 excludes. It needs an ADR-0001 amendment first |
| Who owns the start order and thread constraints | The product, written once and checked by a fixture | Crux, enforced in one place |
| Product code size | About 15 calls in order | One call and a hooks struct |
| Public surface | Several small headers, each a decision | One larger interface. A change breaks every product |
| Fit for product policy | Product decides what starts and where | Crux decides what starts. Kconfig cannot carry the hooks, so a C interface is needed anyway |
| Risk of a wrong order | Real. Mitigated by the fixture, which runs the reference order | Low |
| Licence boundary | Each symbol is separate, SSH stays off by default | One symbol set. Still controllable |
| Evidence needed first | One consumer is known (Controller) | The same sequence needed by at least two products. **Inference:** [Extending Crux](../architecture.md#extending-crux) asks for a real consumer, and there is one |
| Reversibility | Headers can be added one at a time | A start function is hard to take back |

**Recommendation: A.** It is the adopted pattern, it adds the least public surface per step, and it keeps the product's thread and priority choices with the product (ADR-0002 P3 places the services' workers by Kconfig; the product still owns its control cycle). Its weak point is the ordering burden. The fixture of [section 5](#5-boundary-test-plan) is the mitigation: it is the reference order, run on every change. **Alternative:** B, if a second product repeats the same sequence and a reviewed ADR-0001 amendment adds a rule for orchestrating interfaces. The two are not exclusive: B could later be built on top of A's interfaces.

### What route A would publish

Phase 1 only. Each row is a decision for the owner (C2), and each header would pass the six rules.

| Candidate | Publishes | Does not publish | Rule 4 reading |
| --- | --- | --- | --- |
| `<crux/web_zephyr.h>`, `crux::web_zephyr` | `crux_web_core_init`, `crux_web_start`, `crux_web_config_t` (the existing declarations of `components/web/adapter/crux_web_adapter.h`) | nothing else of the adapter header | Binds the listener to the Zephyr HTTP server. No new function |
| `<crux/tls_certificate_zephyr.h>`, `crux::tls_certificate_zephyr` | `crux_tls_certificate_on_change`, `crux_tls_certificate_start`, `crux_tls_certificate_material_take` and their types | nothing else | Hands material to Zephyr's TLS credentials. No new function |
| `<crux/credential_zephyr.h>`, `crux::credential_zephyr` | `crux_credential_start` only | raw read, write, key replace, erase, key parse | Binds the store to two File roots. **Inference:** the product may not need `crux_credential_key_ensure`, because the TLS module ensures its own key at start. Not verified for the SSH host key |
| Web session and management lifecycle | `crux_web_session_init`, `crux_web_session_start_worker`, `crux_web_management_init`, `crux_web_management_start_worker` | session internals | **Not Zephyr-bound.** Rules 1 to 5 do not fit. Publish as ordinary service API under ADR-0001 D4 tiers, or fold into `web_zephyr`. See C2 |
| Network settings apply port | `crux_network_settings_port_apply`, `crux_network_settings_port_running` as a header the **product implements** | the event functions of the same header | Not a binding of a Crux provider to the OS. It is the inverse, a Crux-defined port with a product implementation. The six rules do not cover it. See C3 |
| Access first account | nothing | `crux_access_provision_first_account` | Stays private. Publishing it would let product code create the first administrator. The serial CLI setup, which is Crux code, calls it |

SSH would add `<crux/ssh_zephyr.h>` in phase 3 only.

## 3. The hooks

A **hook** is what only the product can supply. The table says what exists today, and how each route covers it.

| Hook | Today | Route A | Route B |
| --- | --- | --- | --- |
| **First account** | `crux_access_provision_first_account` is private, refuses a non-serial origin, and the CLI setup loop calls it. The serial console is composed in no image ([Secure management in a product](../management-in-a-product.md), [CLI contract](../cli-contract.md)). Test images provision in code | No new symbol. The product composes the serial CLI (`crux_cli_init`, public) and a person types the password. Which serial device: **Not verified**, the console start API was not read | Same. The start function composes the console under Kconfig |
| **Host key** (phase 3) | The provider borrows the key bytes until `stop()`. The integrated image keeps them in a static buffer that is never wiped ([Secure management in a product](../management-in-a-product.md)), while the architecture table says the credential store generates and keeps one across restarts (`docs/architecture.md`, SSH rows). The two records are about different images. **Not reconciled** | `<crux/ssh_zephyr.h>` takes a start config with a host key source. Product supplies none if the store holds it | A hook callback that returns the key. Same open decision |
| **User key** | `crux_access_account_add_key` is public (`access.h:280`). Only test code calls it. #318 asks who calls it and over which channel. #316 asks which key types | Nothing to publish. The product writes the mechanism, for example an authenticated route of its own through `CRUX_WEB_ROUTE`. The decision of #318 is still needed. Whether the route handler has the session that the call needs: **Not verified** | A `register_user_key` hook cannot remove #318 |
| **Certificate registration** | No composition registers the certificate with the listener and no route exists. Both TLS backends read Zephyr's TLS credentials ([HTTPS composition](../https-composition.md)) | A1: the product adapter calls `crux_tls_certificate_material_take`, then `tls_credential_add` with `CRUX_WEB_TLS_SEC_TAG`. Zephyr is called in the product adapter, as the task requires. The key's PKCS #8 bytes then pass through product code. A2 (a new Crux bind function that does the registration) keeps them inside Crux but adds a function, which rule 4 does not allow without a decision | Crux registers it inside the start function. No hook except the sec tag Kconfig |
| **Network apply** | A private port. The composition links `network_settings_no_apply.c`, so nothing is applied to an interface in any image | The product implements `crux_network_settings_port_apply` and `_running` with Zephyr `net_if` in its adapter. The declaration must become public (C3) | A callback pair in the hooks struct. It changes the public service contract |
| **Reboot policy** | Public: `crux_reboot_config_t` with a callback or always-allow policy and wait sources, and `crux_reboot_start`. The reset goes through `crux::system`. No image composes the service, and its routes are in no image ([Reboot service](../reboot-service.md)) | Nothing new. The policy callback is plain C in the domain | Same hook, passed in the struct |

### Start sequence and threads

Route A leaves the product to write this order. It is the order of the test composition, restated with what each step needs. Steps that call a private function today are marked.

| Step | Call | Thread rule |
| --- | --- | --- |
| 1 | `crux_security_event_init` | before the services that raise events |
| 2 | Time and the File roots (`crux_file_zephyr_bind_root`, exists) | |
| 3 | `crux_credential_start(roots)` (private today) | as the store requires |
| 4 | `crux_access_init(policy)` | returns before any other Access operation (`access.h:6`) |
| 5 | `crux_network_settings_boot`, which calls the apply port | before the Configuration service |
| 6 | `crux_config_service_start`, `crux_mgmt_status_start` | |
| 7 | First account through the serial setup | serial channel only |
| 8 | `crux_tls_certificate_start`, then the fingerprint, then `material_take` and `tls_credential_add` (private today) | thread context, returns before any other module operation (`adapter.h:17`), and before the listener |
| 9 | network settings worker, file service, CLI tables, `crux_web_session_init`, then `crux_web_management_init` and its worker (private today) | one thread, after the services (`crux_web_management.h:48`) |
| 10 | `crux_web_core_init` and `crux_web_start` (private today) | after step 9 |
| 11 | SSH, phase 3 only: user keys, then the SSH start | |

Which thread the product calls them from is the product's choice, with the one-thread rule above. Priorities of the services' workers come from the Kconfig of ADR-0002 P3 (`zephyr/Kconfig`, the menu "Priorities of Crux service workers"). #197 lists control jitter with and without management load as an acceptance item. No measurement was read.

## 4. What Crux provides and what a product supplies

| Crux provides | The product supplies |
| --- | --- |
| The service cores, their PSA ports and Zephyr providers, built by the module under Kconfig symbols (**does not exist yet**: phase 0) | The Zephyr devicetree, board, entropy source and `prj.conf` |
| The composition headers of section 2, one per Zephyr-bound start | The start sequence, in its adapter code, from a reference order |
| The route sources (`CRUX_WEB_*_ROUTES`), the reviewed route list and the packaging macros (`cmake/CruxWebRoutes.cmake`, `cmake/CruxWebAssets.cmake`) | Its own routes with `CRUX_WEB_ROUTE`, the frontend, and its reviewed route list |
| The reboot, status, configuration and System service APIs | The reboot policy callback, the network apply port, the user key mechanism, the host key decision |
| A fixture that runs the reference order on `native_sim` | Its own CI, its release rules and its licence choice |

**Frozen headers.** `crux/thread.h`, `crux/sync.h`, `crux/queue.h` and `crux/serial.h` must not appear on the product surface. **Verified:** none of the six web, TLS certificate, credential, Access and web management and session headers of section 1.2 includes one (`grep` of `#include`). I did not check the network settings port header or the SSH headers, and the headers of section 2 do not exist yet, so their includes are a requirement of this proposal, not a fact. **Not avoided today:** the product's `prj.conf` in the reference image names `CONFIG_CRUX_THREAD` and `CONFIG_CRUX_SYNC` (`tests/native/management_composition/prj.conf:34-37`), because the services link them. The proposal is that the service symbols `select` them, so a product's configuration names only service symbols (C6). The template's `check_domain_includes.py --product` already rejects the four headers in product source files.

**No pass-through wrappers.** A composition header publishes an existing declaration. It does not wrap a Zephyr call. `tls_credential_add` and `net_if` stay in product adapters.

**Zephyr calls only in product adapters.** The product's domain stays free of Zephyr and of every `NAME_zephyr.h` header, as the template check requires.

## 5. Boundary test plan

The model is `tests/build/test_file_zephyr_boundary.py` and `tests/build/test_file_zephyr_public.py`. They configure small CMake projects against the real guard and the real target definition without a Zephyr SDK, and they run the same projects against a weakened guard so a rejection is not vacuous. The new tests would go beyond them, because the question is not only "is the target rejected in a portable consumer" but "can a consumer outside the repository compose the services with public targets only".

**T1, Host: each composition target is rejected in portable code.** For each new target, the three scenarios of the File test: direct link, transitive wrapper, usage check. Plus the weakened-guard control (error demoted to a status line, name patterns dropped).

**T2, Host: each header is self-contained and clean.** Compile with `-fsyntax-only -H` as `test_file_zephyr_public.py` does. Assert that the only Crux headers pulled in are the capability's own, that no frozen header and no native header appear, and that the private header no longer declares the same function a second time.

**T3, native_sim: an out-of-tree consumer composes the services.** A fixture project copied outside the repository (as `tests/build/test_external_application.py` asserts with `assertNotIn(REPO, source.parents)`), built like `scripts/test-product-template.sh` does, with `-DZEPHYR_EXTRA_MODULES=<repo>` on `native_sim`. Its CMake links only `crux::` targets of section 2 and its sources include only `<crux/...>` headers. It runs the reference order of section 3, serves HTTPS, and a test client logs in over TLS. Assertions on the build:

- `compile_commands.json` has no `-I` path of the form `components/*/(src|ports|adapter|management|session)` for the fixture's own sources.
- The product-mode include check finds none of the four frozen headers.
- The ELF has no wolfSSL, wolfSSH or wolfPSA symbols, and the generated `.config` has no `CONFIG_WOLF*` set.

**T4, the negative cases that must fail.** Each is a one-line change to the fixture that must fail to configure or compile, with the guard's message:

| Variant | Must fail because |
| --- | --- |
| `#include "crux_web_adapter.h"` with `components/web/adapter` on the include path | a private header |
| `target_link_libraries(app PRIVATE crux_web_adapter)` | a target with no `crux::` name |
| `#include <crux/thread.h>` in a product adapter | a frozen header |
| `#include <crux/web_zephyr.h>` in the domain directory | a composition interface in the domain |
| Reaching `components/credential/adapter` | the credential boundary rule |

Each negative also runs against a weakened guard and must then pass, as the File test does.

**T5, manifest: the licence boundary holds.** A consumer `west.yml` that imports `crux-pilot` with the narrowing of section 6. `west list --all` must contain none of `wolfssl`, `wolfssh`, `wolfpsa`, and a control that enables the group without narrowing must show them active and fail. Needs `west`, so it runs where `west` is installed. The Host job has none (`tests/build/test_product_template.py` skips without it).

**What the guard needs.** A way to say "composition consumer": it accepts the named `*_zephyr` targets and rejects `adapter`, `management` and `session` paths. Whether that is one more alternative in the regex at `cmake/CruxConsumerBoundary.cmake:130` or a new function was not traced, because a change there could flag Crux's own targets that link `crux_web_adapter` publicly. That is implementation work for the follow-up.

**Where it runs.** T1 and T2 in the Host job. T3 and T4 in a Zephyr shard next to `scripts/test-product-template.sh` (called at `.github/workflows/zephyr.yml:269`). T5 where `west` exists. Per the owner's rule, push the Draft PR and let CI run the Zephyr parts, not local amd64 emulation.

## Phasing and the licence boundary

| Phase | Content | Needs |
| --- | --- | --- |
| 0 | Zephyr module integration of the service cores and PSA ports, under Kconfig symbols. Shared by both routes. The fixture of T3 | The owner's decisions C1 and C2 |
| 1a | HTTPS on the Mbed profile on `native_sim`: Access, credential store, security events, Configuration, status, network settings, TLS certificate, System, web sessions and routes. First account by the test declaration, `native_sim` only | Phase 0, T1 to T5 |
| 1b | The serial CLI with the first-account setup. **Before any board image.** See C5 | Phase 1a |
| 2 | The rest of the CLI, and reboot with its policy | Phase 1b |
| 3 | SSH, the command line over SSH and SFTP on a **manual** job that publishes nothing | The owner's decisions on the patch route and the licence (C8), #316, #318 |

Why 1b comes before a board: `crux_access_provision_first_account` refuses a non-serial origin (`components/access/adapter/crux_access_adapter.h:108-120`). Without a serial setup step, a real HTTPS image has no first account, and the only alternative is a password embedded from a file, which is a bench test form accepted under a bench declaration only ([bench first account](../rt1060-bench-first-account.md)).

### Keeping the licence boundary

Constraints, not decisions: SSH and SFTP need wolfSSH, wolfSSL and wolfPSA (GPL-3.0-or-later or a commercial licence). HTTPS on the Mbed profile is the licence-clear first step. The terms of the wolfSSL TLS socket patch are unconfirmed, so the Wolf profile cannot be carried publicly ([Product composition](../product-composition.md), licence bullet). The module also refuses two TLS stacks in one image and refuses `CONFIG_CRUX_SSH` on the Mbed profile (`zephyr/CMakeLists.txt:162-175`, **Verified**), so **SSH implies the Wolf profile and therefore the patch** (**Inference**; whether SSH alone needs the patch was not checked).

Three mechanisms, and they stack:

1. **Manifest narrowing.** Today Mbed TLS, TF-PSA-Crypto and the three wolf projects are in one group, `crux-secure-management` (`west.yml:28-58`, **Verified**), and the group is off by default (`west.yml:7`). Enabling it fetches all five. `import: name-blocklist: [wolfssl, wolfssh, wolfpsa]` on the `crux-pilot` project, with `+crux-secure-management`, is **Reported** by the Controller session (note `t3-mbed-narrowing`, `west` 1.4.0, `west list --all` and `west manifest --resolve`) to give `mbedtls` and `tf-psa-crypto` and no wolf project. It also reports that the same result holds with a name allowlist, which must name `zephyr` and every needed project, and that the narrowed manifest and the baseline manifest were byte-identical at the Controller's pin. I read `west.yml` and `.github/workflows/psa-crypto.yml:60` (`west config manifest.project-filter -- +mbedtls,+tf-psa-crypto`, **Verified**), which uses the same idea on Crux's side. **I did not run `west`: it is not installed here, and I did not download it.** The Controller note also did not run a full `west update`. T5 is where this gets proven. An alternative that needs no product change is to split the group in Crux's manifest into an Mbed group and a Wolf group. That changes the pins of every consumer and is a separate decision (C7).
2. **Kconfig.** `CONFIG_CRUX_SSH` defaults off, the module refuses it without the wolf symbols, and a product that does not set it builds none of it. A dependency that is not met only warns in Zephyr and the symbol silently becomes `n` ([Secure management in a product](../management-in-a-product.md), Kconfig table), so T3 asserts the absence in the `.config` and in the ELF.
3. **CI.** Default jobs never enable the group. The phase 3 job is `workflow_dispatch` only, uploads no artifact and attaches nothing to a release. T5 fails the default job if a wolf project appears in the resolved manifest.

## Follow-up changes

None is part of this record. Each needs its own reviewed change after the owner decides. Protected paths that these would touch are listed so approval can be asked per change.

1. The decision record's acceptance, or its rejection, and the `docs/decisions/README.md` row.
2. Phase 0: Kconfig and `zephyr/CMakeLists.txt` for the services. `zephyr/` is not a protected path. **Protected, expected:** none for this step.
3. The composition headers and targets of section 2, one capability per change, each with T1 and T2. **Protected, expected:** none, unless the placement note is added to `CONTRIBUTING.md`.
4. The "composition consumer" mode of the guard: `cmake/CruxConsumerBoundary.cmake` (**protected**), and the placement note of rule 1 in `CONTRIBUTING.md` (**protected**).
5. The fixture and T3 to T5: new files under `tests/` (not protected) and a Zephyr shard entry in `.github/workflows/zephyr.yml` (**protected**).
6. A new script `scripts/test-consumer-composition.sh` (a new check script may be added within the task).

## Open questions for the owner

The recommendation comes first in each row.

| # | Question | Recommendation | Reason, and the alternative |
| --- | --- | --- | --- |
| C1 | Which route: A or B? | **A** | The adopted rules cover it, it adds surface one capability at a time, and the product keeps its thread choices. B needs an ADR-0001 amendment and a second consumer first |
| C2 | Which private start calls become public, and as which kind? | The subset of section 2: `web_zephyr`, `tls_certificate_zephyr`, `credential_zephyr` (start only), and the web session and management lifecycle as ordinary service API | Keeps first-account provisioning and raw credential access private. Alternative: publish whole adapter headers, which is simpler and widens the surface to credential record access |
| C3 | The form of the network apply port: a header with two declarations the product implements, or a callback struct? | **Declarations** | No behaviour change, and no break of the public `crux_network_settings_boot`. The six rules do not cover a product-implemented port, so a sentence in `docs/composition-interfaces.md` would be needed |
| C4 | Certificate registration: A1 (the product adapter calls `tls_credential_add`) or A2 (a new Crux bind function)? | **A1** for phase 1 | A1 adds no function and keeps Zephyr calls in the product adapter. A2 keeps the key inside Crux and is the safer shape if a second product appears. Security-sensitive: your decision |
| C5 | Does phase 1 include the serial CLI first-account step before any board image? | **Yes** | Without it a real image has no first account. Alternative: HTTPS only on `native_sim` and no board image until phase 1b, which loses nothing but time |
| C6 | Should service Kconfig symbols `select` Thread and Sync, so a product's `prj.conf` names only service symbols? | **Yes** | Keeps the frozen symbols off the product configuration. ADR-0003 keeps them internal to the services. Alternative: leave them in the product `prj.conf`, as the reference image does |
| C7 | Licence boundary: a blocklist in the product manifest, an allowlist, or a split of the Crux group? | **Blocklist now, with T5** | No Crux change and **Reported** to work. The allowlist is safer against drift and needs edits when a project is added. A split is cleanest and changes every consumer's pins |
| C8 | Phase 3: when, and on what terms? | **Not planned in this proposal.** Ask again when the wolfSSL patch terms are confirmed | SSH implies the Wolf profile and the patch, which cannot be carried publicly today. Fixing the manual job's design before that risks a design that cannot run |
| C9 | Approve protected-path changes per follow-up, or in one block? | **Per follow-up** | AGENTS.md asks for approval that covers each change. The list under [Follow-up changes](#follow-up-changes) shows which ones touch protected files |

### The owner's decisions

Pending. This section stays empty until the owner decides. Accepting this record decides C1 to C9 as recommended unless the owner says otherwise. It changes no code, check or protected path. Each follow-up is its own reviewed change ([the convention](README.md#convention)).

## What was run

On native macOS, in the worktree at `a72032b2`: `git fetch`, reads of the files cited above, `grep`, `gh api` for the Controller finding, and `gh issue view` for the five issues. Not run: `west`, any CMake configure, any build, `native_sim`, any test, any hardware command. The documentation link check is recorded in the pull request.

## Record

| Item | Value |
| --- | --- |
| Baseline | `a72032b2f29c8c4579de8de07a43b40f19bb9516` |
| Skills and agents used | `ecc:architecture-decision-records` (record format, with this repository's convention taking precedence), `ecc:search-first` (search of existing tests, targets and decisions before writing, with no install or dependency) |
| Model | `claude-sonnet-5-5`, Claude Code |
| Independent review | The `reviewer` agent, verdict in the pull request |
