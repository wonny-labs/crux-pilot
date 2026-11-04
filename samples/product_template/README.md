# Product template

A starting point for a product repository built on Crux. The repository is its own west manifest repository. Crux and Zephyr are projects in its workspace.

No Crux release tag exists yet. The [Product Composition Guide](https://github.com/wonny-labs/crux-pilot/blob/main/docs/product-composition.md) (`docs/product-composition.md` in `crux-pilot`) explains the choices.

| File | Purpose |
| --- | --- |
| `west.yml` | Imports `crux-pilot`, with comments for the optional group, a product-only module and an override |
| `app/` | A Zephyr application that links `crux::time` |
| `.gitignore` | Keeps `build/` out of the source tree that the build record hashes |

## Start a product

Copy this directory into an empty workspace directory, rename `product-x` in `west.yml`, and make it a Git repository. Replace `<board>` with your board target.

~~~sh
mkdir acme-workspace
cp -R crux-pilot/samples/product_template acme-workspace/product-x
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

Pin a Crux tag or SHA in `west.yml` before you make a release.
