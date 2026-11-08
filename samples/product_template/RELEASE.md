# Release record: `<product>` `<product-version>`

A template. Copy this file for each release, fill in every `<placeholder>` and keep the copy with the release bundle. Delete this paragraph from the copy. Nothing here is a Crux commitment: the Product Composition Guide, linked under Release steps, holds the practice, and this file only gives a product the places to record what it did and decided.

Treat the filled-in record as an internal item. Check it for private strings before any part of it goes outward, as for every other item of the bundle.

## Release identity

- Product: `<product>`
- Product version: `<product-version>`
- Release date: `<YYYY-MM-DD>`
- Prepared by: `<role or team>`

## Dependency inventory

List every upstream project of the release: each project in the frozen manifest (`locks/west.frozen.yml`), each product-only module and each delivery that no manifest covers. Fill the table from `west crux-pins` and the frozen manifest, and check the licences against the SBOM. The last column is optional: fill it for an upstream that publishes an end-of-support date, and leave it empty otherwise.

| Project | Source | Pinned revision | Licence | End of support (optional) |
| --- | --- | --- | --- | --- |
| `crux-pilot` | `<remote or URL>` | `<commit SHA>` | `<SPDX identifier>` | `<date, or empty>` |
| `zephyr` | `<remote or URL>` | `<commit SHA>` | `<SPDX identifier>` | `<date, or empty>` |
| `<project>` | `<remote or URL>` | `<commit SHA>` | `<SPDX identifier>` | `<date, or empty>` |

## Modified upstreams

Record the route chosen for each upstream that the product changes, and why that route. The routes are the ones that the guide names in its section on modifying third-party code, and the guide calls them provisional:

- Vendor branch, for vendor source delivered to you.
- Commercial edition under a different project name, for a module that Crux pins.
- Soft fork with tagged commits, for a public upstream that goes into a release.
- `west patch`, only for a short-term development check, never for a release.
- Binary-only delivery, in internal git or through `west blobs`, with a hash and a release record.
- Kept outside the checkout, identified by a committed hash list.

| Project | Route | Why this route | Carried-changes record |
| --- | --- | --- | --- |
| `<project>` | `<one route from the list>` | `<reason>` | `<where the list of carried changes is kept>` |

If nothing is modified, replace the table with the sentence "Nothing is modified in this release."

## Support period

The product sets the support period. Crux does not set one for a product, and this template names none.

- Support period: `<set by the product>`
- Decided by: `<role set by the product>`
- Decided on: `<YYYY-MM-DD>`
- Where it is recorded: `<reference to the decision>`

Compare the period with the optional end-of-support column of the dependency inventory.

## Release steps

Follow the release section of the guide at the Crux revision that this release pins. Replace `<crux-revision>` with that tag or commit SHA. Never link a moving branch from a record, because the text behind the link would change under it.

- Guide: `https://github.com/wonny-labs/crux-pilot/blob/<crux-revision>/docs/product-composition.md`
- Release section: `https://github.com/wonny-labs/crux-pilot/blob/<crux-revision>/docs/product-composition.md#release`

The steps in order, with the one command that each is known by. Tick each step when it is done.

1. [ ] Pin a Crux tag or commit SHA in `west.yml`, and record it below as the Crux revision.
2. [ ] Run `west crux-pins`, so that a failure does not burn a tag.
3. [ ] Freeze the manifest with `west manifest --freeze --active-only`, commit the lock on a release branch and tag it.
4. [ ] In a fresh workspace, initialise from the tagged frozen manifest, run `west update` and run `west crux-pins` again.
5. [ ] Build with `-DCRUX_REQUIRE_VERIFIED_ZEPHYR=ON`, or record here why another Zephyr release is used on purpose.
6. [ ] Write the build record with `crux-pilot/scripts/record-build-provenance.py`.
7. [ ] Write the SBOM with `west spdx`, and reduce it before it goes outward.
8. [ ] Assemble the release bundle and check it, as described below.
9. [ ] Fill in the revision and the checks below.

## Bundle contents

A release bundle is a folder of text items and one `release-manifest.json` that holds the hash of each other item.

**Hashes only.** A public product repository publishes the SHA-256 of the firmware image, in the release manifest, and nothing more. The image, the `.elf` file and the `.map` file never appear in its bundle, and are never a release asset or a CI artefact. Customers receive the image through a private delivery channel. A bundle of hashes only is the allowed form, and the only form that the provisional checker accepts.

Mark each item as in this bundle, as a hash only, or as not in it.

| Item | Audience | In this bundle? (yes, hash only or no) |
| --- | --- | --- |
| Firmware image | Outward to customers through a private delivery channel only. In a public product repository, its SHA-256 only | `<yes, hash only or no>` |
| `licences/` | Outward | `<yes or no>` |
| Outward SBOM | Outward, reduced by the product from the full set | `<yes or no>` |
| `release-manifest.json` | Internal | `<yes or no>` |
| Build record | Internal | `<yes or no>` |
| Lock file | Internal | `<yes or no>` |
| Pin report | Internal | `<yes or no>` |
| Carried-changes list | Internal | `<yes or no>` |
| Full SBOM | Internal, and given to authorities on request | `<yes or no>` |
| Debug files (`.elf` and `.map`) | Internal only, and never in a public product repository | `<no, or a private location>` |

The manifest follows the schema in `docs/schemas/release-manifest.schema.json` of `crux-pilot`, and the checker is `scripts/check-release-bundle.py`. Both are provisional and not frozen before a product's first real release, so their shape can still change. Run the checker as `python3 crux-pilot/scripts/check-release-bundle.py <bundle-dir>`. Its limits are in `docs/release-bundle-checker.md`.

Fields that only this product needs go in the `extensions` object of the manifest.

## Release revision

The bundle is only as exact as these values.

- Release tag: `<product repository tag>`
- Product commit: `<commit SHA that the tag points to>`
- Crux revision: `<tag or commit SHA pinned in west.yml>`
- Zephyr revision: `<commit SHA from the frozen manifest>`
- Image SHA-256: `<hash of the firmware image>`
- Firmware identity: `<firmware_identity from the build record>`

A file in the tagged tree cannot name its own commit. Record the product commit in the copy that is kept with the bundle, not in the tagged tree.

## Checks run

Record every check of this release, including those that failed. A check that was skipped or did not run is listed as such with the reason, and is never counted as a pass.

| Check | Command | Result | Run by |
| --- | --- | --- | --- |
| `west crux-pins` | `west crux-pins` | `<result>` | `<person or automation>` |
| Release bundle checker | `<command>` | `<result>` | `<person or automation>` |
| `<check>` | `<command>` | `<result>` | `<person or automation>` |

- Skipped checks and why: `<list, or "none">`
- Checks that did not run and why: `<list, or "none">`

## Vulnerability check

Placeholder. A procedure for checking the dependencies against known vulnerabilities in the SBOM before a release is not defined yet. It is an open follow-up of the guide. Record "not run" here until a procedure exists, and do not treat this line as a check.

- Result: `<not run>`
