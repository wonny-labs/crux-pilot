# Product template

This template moved from `samples/product_template` to `templates/product` on 9 November 2026. Update a copy or a link that names the old path.

A starting point for a product repository built on Crux. The repository is its own west manifest repository. Crux and Zephyr are projects in its workspace.

No Crux release tag exists yet. The [Product Composition Guide](https://github.com/wonny-labs/crux-pilot/blob/main/docs/product-composition.md) (`docs/product-composition.md` in `crux-pilot`) explains the choices.

| File | Purpose |
| --- | --- |
| `west.yml` | Imports `crux-pilot`, with comments for the optional group, a product-only module and an override |
| `app/` | A Zephyr application, the execution shell, that links `crux::time` and calls the domain |
| `domain/` | The product's own behaviour as plain C, with one product-owned port header |
| `check_domain_includes.py` | The domain include check |
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
| Domain | `domain/` | ISO C headers, non-frozen public `<crux/NAME.h>` headers and its own headers | The product's behaviour. No thread, no lock. It reaches the outside through `product_port.h` only |
| Shell | `app/src/` | Zephyr, public Crux headers and the domain's headers | Threads, queues, locks, the implementation of the port and the adapters that Crux services call |

Run the check from the repository root after each change to the domain. It needs Python 3.9 or later and nothing else.

~~~sh
python3 check_domain_includes.py
~~~

It fails on `<crux/thread.h>`, `<crux/sync.h>`, `<crux/queue.h>`, `<crux/serial.h>` and every `<zephyr/...>` header in `domain/`, in either quoted or angle form and in any file. It also fails on a quoted include of a file outside `domain/` and on an include operand that it cannot read, such as a macro. The four Crux headers are frozen for new product code, and the domain is plain C. The check is lexical, so review still matters: it cannot see an include path that CMake adds, and the template adds none. Build the domain on the Linux Host with a fake of `product_port.h` as well, because a Zephyr header does not exist there. Crux's own test builds the shipped domain that way and runs it against a fake port.

The check does not apply to the shell. A product that has a reason to include a frozen header in its domain, such as a ported runtime that already uses Crux Thread, edits the check and records the reason in its repository.

## Shared-state example

`app/src/main.c` shows how product state is shared with threads that Crux services start. A command handler runs on the thread of its console or SSH session, a web handler on the Zephyr HTTP server thread, the reboot busy check on the reboot worker, and a status read on the caller's thread.

1. One owner thread, `product_owner_entry`, is the only code that calls the domain.
2. A status read, `product_status_read`, loads a snapshot that the owner stores in a C11 atomic after each step. It takes no lock and never calls the domain.
3. A command, `product_command_set_limit`, posts a message to the owner's `k_msgq` without waiting. The owner applies it between two steps.
4. A lock would live in the shell. This example needs none, because the snapshot is one value.

`main()` stands in for the callbacks of a service, so the example needs no extra Crux component. A real handler calls the same adapter and does nothing more. It is an example, not a framework: replace the stand-in port with a driver, and replace the one command with the product's own.
