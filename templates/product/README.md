# Product template

This template moved from `samples/product_template` to `templates/product` on 9 November 2026. Update a copy or a link that names the old path.

A starting point for a product repository built on Crux. The repository is its own west manifest repository. Crux and Zephyr are projects in its workspace.

No Crux release tag exists yet. The [Product Composition Guide](https://github.com/wonny-labs/crux-pilot/blob/main/docs/product-composition.md) (`docs/product-composition.md` in `crux-pilot`) explains the choices.

| File | Purpose |
| --- | --- |
| `west.yml` | Imports `crux-pilot`, with comments for the optional group, a product-only module and an override |
| `app/` | A Zephyr application, the execution shell, that links `crux::time` and calls the domain |
| `domain/` | The product's own behaviour as plain C, with one product-owned port header |
| `check_domain_includes.py` | The domain include check, and with `--product` the include check of the whole product repository |
| `RELEASE.md` | A [release record](RELEASE.md) to copy for each release: dependency inventory, routes for modified upstreams, support period, release steps, bundle contents, revision and checks |
| `.gitignore` | Keeps `build/` out of the source tree that the build record hashes |

## Start a product

Copy this directory into an empty workspace directory, rename `product-x` in `west.yml`, and make it a Git repository. Replace `<board>` with your board target.

~~~sh
mkdir acme-workspace
cp -R crux-pilot/templates/product acme-workspace/product-x
cd acme-workspace
git -C product-x init
git -C product-x add .
git -C product-x commit -m "feat: start product-x"
west init -l product-x
west update
west crux-pins
west build -b <board> -d product-x/build/app product-x/app
~~~

`west crux-pins` compares the workspace with the revisions Crux pins, and fails on a difference with no declared reason. The build warns if the workspace uses another Zephyr release, and stops with `-DCRUX_REQUIRE_VERIFIED_ZEPHYR=ON`. The guide lists both checks, how to modify third-party code and how to make a release.

Pin a Crux tag or SHA in `west.yml` before you make a release. Then copy `RELEASE.md` and fill it in for that release. It leaves the support period to the product, allows a bundle of hashes only, and links the guide at a pinned Crux revision. The release bundle schema and checker it names are provisional.

## Zones and the domain include check

The template has two zones, after the three questions of [Choosing an API](https://github.com/wonny-labs/crux-pilot/blob/main/docs/choosing-an-api.md).

| Zone | Where | May include | Owns |
| --- | --- | --- | --- |
| Domain | `domain/` | ISO C headers, public `<crux/NAME.h>` headers of the contracts that are not frozen (Time, Logging, File and so on) and its own headers | The product's behaviour. No thread, no lock. Whatever else it needs from outside the domain, such as a sensor, comes through a port declared in `product_port.h` |
| Shell | `app/src/` | Zephyr, public Crux headers and the domain's headers | Threads, queues, locks, the implementation of the port and the adapters that Crux services call |

Run the check from the repository root after each change to the domain. It needs Python 3.9 or later and nothing else.

~~~sh
python3 check_domain_includes.py
~~~

It fails on `<crux/thread.h>`, `<crux/sync.h>`, `<crux/queue.h>`, `<crux/serial.h>`, every `<zephyr/...>` header and the Crux composition interfaces `<crux/NAME_zephyr.h>` and `<crux/NAME_linux.h>` in `domain/`, in either quoted or angle form and in any file. It also fails on a header name with a `.` or `..` component, on a quoted include of a file outside `domain/`, on an include operand that it cannot read, such as a macro, and on a domain directory that holds no file. The four Crux headers are frozen for new product code, the composition interfaces name a provider, and the domain is plain C.

The check is lexical and lists only these names. Other native headers (SoC, CMSIS, vendor HAL, POSIX under `CONFIG_POSIX_API`) are caught only by the Host build of the domain, because they do not exist there, so build the domain on the Linux Host with a fake of `product_port.h` as well. Crux's own test builds the shipped domain that way and runs it against a fake port. A lexical check cannot see an include path that CMake adds, and the template adds none, so review still matters.

Without an option the check does not look at the shell. The freeze (P2 of [ADR-0002](https://github.com/wonny-labs/crux-pilot/blob/main/docs/decisions/0002-platform-direction-zephyr-execution-crux-services.md)) exempts only D10's Build B, which is a separate repository. Do not edit the check to allow another case. A new case is an owner decision.

### Check the whole product repository

A product repository, or a copy of this template, checks all its sources with `--product`: the domain and the adapters in the shell. Run it from the repository root, in CI and before a review.

~~~sh
python3 check_domain_includes.py --product .
~~~

It reads every `.c`, `.h`, `.cc`, `.cpp`, `.cxx`, `.hh`, `.hpp`, `.hxx`, `.inc` and `.S` file under the root, in the same lexical way, and applies two rules:

- **Everywhere**, adapters included, it fails on `<crux/thread.h>`, `<crux/sync.h>`, `<crux/queue.h>` and `<crux/serial.h>`, in quoted or angle form and in every spelling that the domain check reads. An adapter may include `<zephyr/...>` headers, the composition interfaces and the other public Crux headers. An operand that is a macro is not a violation in an adapter, because Zephyr code uses macros, but a header name that opens and does not close is.
- **In `domain/`** it keeps every rule above: no `<zephyr/...>`, no composition interface, no operand it cannot read, and no quoted include of a file outside the directory. `domain/` is the default and is used only if it exists. Name other plain C directories with `--domain DIR`, once for each, and an option that names a missing directory is an error.

Code that already uses a frozen header and has not moved yet is listed once, so that the check can run in CI today and still catches every new use: add `--exclude PATH` for each legacy file or directory, relative to the root. An excluded path must exist and cannot cover a domain directory, and the check prints what it excluded. Remove the entry when the code moves to Zephyr.

~~~sh
python3 check_domain_includes.py --product . --exclude app/src/legacy_threads.c
~~~

It never reads `.git`, `.west`, `build` or a symbolic link to a directory, and it exits with status 2, not 0, on a root without a source file. It is lexical like the domain check. For code that is compiled, the opt-in compiler warning of the frozen headers is the second layer: it sees what the preprocessor sees, including a header that a macro or another header pulls in. Define `CRUX_WARN_FROZEN_HEADERS` for a target that compiles only the product's own sources, as in [Choosing an API](https://github.com/wonny-labs/crux-pilot/blob/main/docs/choosing-an-api.md#frozen-for-new-product-code-thread-sync-queue-and-serial).

~~~cmake
target_compile_definitions(product_adapters PRIVATE CRUX_WARN_FROZEN_HEADERS)
~~~

The warning is silent in the targets that Crux's own CMake creates. A Zephyr image usually adds the sources of Crux services to `app`. Those sources include frozen headers on purpose, so they warn if you define the macro on `app` (and fail under the template's `-Werror`). Put the product's adapters in their own library, or accept the warnings from Crux's sources, and rely on `--product` for `app`.

## Shared-state example

`app/src/main.c` shows how product state is shared with threads that Crux services start. A command handler runs on the thread of its console or SSH session, a web handler on the Zephyr HTTP server thread, the reboot busy check on the reboot worker, and a status read on the caller's thread.

1. One owner thread, `product_owner_entry`, is the only code that calls the domain.
2. A status read, `product_status_read`, loads a snapshot that the owner stores in a C11 atomic after each step. It takes no lock and never calls the domain.
3. A command, `product_command_set_limit`, posts a message to the owner's `k_msgq` without waiting. The owner applies it between two steps. The queue is four messages deep: a command that arrives when it is full is refused with `-ENOMSG` and is not applied, so the handler reports "busy" to its caller and does not wait.
4. A lock would live in the shell. This example needs none, because the snapshot is one value.

`main()` stands in for the callbacks of a service, so the example needs no extra Crux component. A real handler calls the same adapter and does nothing more. It is an example, not a framework: replace the stand-in port with a driver, and replace the one command with the product's own.
