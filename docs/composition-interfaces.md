# Composition interface rules

Status: **adopted** by the owner's acceptance of [ADR-0001](decisions/0001-application-boundary-and-api-stability.md) on 12 November 2026 (D3), and written down here as its follow-up change 2. This document is guidance only. It changes no code, check, contract or protected path, and it authorises no hardware operation. The rules in the table come from [Decision 2](decisions/0001-application-boundary-and-api-stability.md#decision-2-where-zephyr-is-allowed-and-how-that-is-enforced) of the ADR. Where this page says what exists today, it was checked against the repository on the date of its change, and the checks are named.

## What a composition interface is

A **composition interface** is the one formal way for a product's platform composition code to reach a Crux provider that must be bound to Zephyr resources: a root, a device, an instance. It is not a portable contract. It names the OS on purpose, so that the [native-free consumer check](native-interface-policy.md) rejects it in portable code. The first and so far only Zephyr example is `crux::file_zephyr` with `<crux/file_zephyr.h>`, described in [File root binding](file-composition-interface.md). The Linux Host counterpart is `crux::queue_linux`, see the [Linux Host nuance](#linux-host-composition-targets).

A product has two routes to Zephyr, and neither is an exception to the [three layers](architecture.md#three-layers-for-a-product-adr-0001):

1. **The product needs a Zephyr feature that Crux does not offer.** It writes an adapter behind a port that it owns. That needs no Crux change and no approval.
2. **Composition code must bind a Crux provider to Zephyr resources.** It uses a composition interface, under the rules below.

## The six rules

| # | Rule | Where it holds today (`crux::file_zephyr`) |
| --- | --- | --- |
| 1 | Target `crux::<capability>_zephyr`, and header `<crux/<capability>_zephyr.h>` under `components/<capability>/ports/zephyr/include/crux/` | Yes: target `crux::file_zephyr`, header `components/file/ports/zephyr/include/crux/file_zephyr.h` |
| 2 | An INTERFACE target that adds the header's include root and links the portable target. It does not link the provider, which Kconfig selects | Yes: `crux_file_zephyr_composition` in `components/file/ports/zephyr/CMakeLists.txt` is an INTERFACE target. It adds `ports/zephyr/include` and links `crux_file`, the target behind `crux::file`. The provider `crux_file_zephyr` is a separate STATIC library that the module appends to the Zephyr link |
| 3 | The native-free consumer check rejects it by name, and by path where the target has an include root. A negative test shows that for a direct link, a transitive wrapper and the usage check | Yes: `tests/build/test_file_zephyr_boundary.py` |
| 4 | It binds, configures or selects a provider: a root, a device, an instance. It adds no behaviour that the portable contract lacks | In substance: the header publishes only the three declarations that already existed, and no new function ([File root binding](file-composition-interface.md#decisions-taken), decision 3) |
| 5 | It may include a Zephyr header or name a Zephyr type, such as `const struct device *`, only where the binding needs one | New in the ADR. `<crux/file_zephyr.h>` needs none: it includes only `<crux/file.h>` |
| 6 | It follows the same stability tiers as any public interface ([Decision 4](decisions/0001-application-boundary-and-api-stability.md#decision-4-api-stability-and-change-policy)). A change forced by a Zephyr baseline move is listed as a breaking change | New in the ADR. No tier is assigned yet: the tier table is the ADR's follow-up change 5, so until it lands the [File root binding](file-composition-interface.md#what-kind-of-interface-this-is) statement of "the same naming and compatibility rules as any public Crux interface" stands |

Why rule 5 exists, in the ADR's words: composition code is Zephyr code already, so passing a device is more honest than inventing an identifier.

### Where the header lives

Rule 1 puts the header under `components/<c>/ports/zephyr/include/crux/`, not under `components/<c>/include/crux/`. The [File root binding](file-composition-interface.md#decisions-taken) justified this for one header (its decision 5): the header names a provider, so it stays with the port and reaches a product only through the composition target, while the portable `<crux/<capability>.h>` header is unchanged. Rule 1 extends that placement to every composition interface.

[CONTRIBUTING](../CONTRIBUTING.md#public-headers-and-module-dependencies) places public headers in a component's `include/crux/`, and does not name this second location. Whether it should is a follow-up change that needs the owner's approval, because CONTRIBUTING is a protected path. It is **not** part of this document's change, and until it is made the placement rests on the File root binding's recorded decision and the ADR.

### Linux Host composition targets

Rules 3, 4 and 6 also cover the Linux Host composition targets, such as `crux::queue_linux`, which name `linux` in place of `zephyr`. **Rule 2 does not apply to them.** On Linux Host no Kconfig selects a provider, so the composition target is the provider itself: in `components/queue/CMakeLists.txt`, `crux::queue_linux` is an ALIAS of the STATIC provider library `crux_queue_linux`. Its negative test, `test_linux_composition_targets_are_rejected_in_portable_consumers` in `tests/build/test_consumer_boundary.py`, covers a direct link, an INTERFACE wrapper, a STATIC wrapper and an application alias. It does not run the usage check on the target, because the portable-consumer guard rejects the target by identity first (the ADR's reading; the test asserts the rejection, not the order of checks).

## What is not an escape hatch

- **Private headers and `ports/` include paths**, such as `file_native.h`. A product never uses them, and they are not composition interfaces. A product that finds it needs one reports it.
- **A missing composition interface** is a Crux gap. It is reported as an issue against `crux-pilot`, as `crux-controller` already does.

## Adding a composition interface

Use these rules for the next one, together with the steps in [Extending Crux](architecture.md#extending-crux): a real consumer needs it, and its minimum contract can be tested. A composition interface also needs the rule against pass-through wrappers of native APIs in [CONTRIBUTING](../CONTRIBUTING.md#making-changes): it must bind or select something, and add no behaviour of its own (rule 4).

Before a new composition interface is proposed, check it against the table above, then add the same negative boundary test as `tests/build/test_file_zephyr_boundary.py` shows. A new public header, target or tier is a public interface decision, so it follows the escalation rules of [AGENTS.md](../AGENTS.md).

## What this document does not do

- It adds no composition interface, check or test, and changes no file under `components/`, `tests/` or a protected path.
- It does not assign stability tiers (ADR follow-up 5) or add the product template's example include check and Host build of the domain (follow-up 3).
- It does not change CONTRIBUTING (see [Where the header lives](#where-the-header-lives)).
