# Stage 0: baseline and boundaries (read-only, collected 1 November 2026)

| Item | Value | Source |
| --- | --- | --- |
| crux-pilot `origin/main` | `f67e22a5bf49874aab18f2dbde7ac239fec689d8` (21:24:16 UTC, #144) | `git fetch -q origin`, `git rev-parse origin/main` |
| Plan baseline | `bb7f425`. `origin/main` is 4 commits ahead of it. | Plan header, `git log bb7f425..origin/main` |
| Local clone state | Working tree on an unrelated documentation branch, `git status --short` empty. Not checked out or pulled. All reads use `origin/main`. | `git status`, `git rev-parse` |
| `west.yml` sha256 at `origin/main` | `5f5e7f7b436b489ffb38ab4973998e64a1714ba8d4c0541ff0d79f4ff907f550` | `git show origin/main:west.yml` |
| Zephyr pin | `revision: 684c9e8f32e4373a21098559f748f06915f950c9` (v4.4.0 per the [plan](../../plans/pre-m8-investigation-plan.md)) | `west.yml` lines 7 to 10 |
| Pinned Zephyr `west.yml` sha256 | `e16b79f1ba532736bcfb729629acff926e4718ba01f57a8c5c2a735f465e50b0` | Raw GitHub copy of the pinned file (`gh api` returned 403 for `zephyrproject-rtos/zephyr`) |
| MCUboot pin | `ee39e2d694bd827ffd1bebbce2f571a9154e6ec2`, path `bootloader/mcuboot`, group `bootloader` | Pinned Zephyr `west.yml` lines 337 to 341: `- name: mcuboot` / `revision: ee39e2d694bd827ffd1bebbce2f571a9154e6ec2` / `path: bootloader/mcuboot` / `groups:` / `- bootloader` |
| Issue #141 | OPEN. Title: "Investigate verified boot and firmware update feasibility for RT1060-EVKB". No milestone, 0 comments, updated 2026-11-01T21:36:22Z. | `gh api repos/wonny-labs/crux-pilot/issues/141` |
| Open PR covering this work | None found. 15 open PRs were checked by title and head branch. | `gh api repos/wonny-labs/crux-pilot/pulls?state=open` |
| PR #139 | Draft. Trust-boundary step in capability guidance. Not related. | Open PR list |
| PR #148 | Draft. Marks RT1170-EVK as deferred. Its body says the owner deferred RT1170-EVK on 1 November 2026 and that #141 was updated to match. | Open PR list, PR body |
| PR #147 | Draft at the time of reading. Adds an RT1060-EVKB microSD composition. Overlaps the staging-medium candidate (#14) in Stage 2. Check before Stage 2. | Open PR list |
| PR #63, #70 | Draft historical M4 ROM experiments. Not reused, per plan. | Open PR list |

Gate status: Stage 0 collected. Owner approval A0 is not evidenced by this report.

## Later correction

| Item | Correction |
| --- | --- |
| crux-pilot `origin/main` | After this record was collected, #147 merged and `origin/main` moved from `f67e22a5bf49874aab18f2dbde7ac239fec689d8` to `f57ff22`. Stage 2 read the repository at `f57ff22`. Stage 3 and Stage 4 cite `f67e22a`. The files they cite were not checked again at `f57ff22`. |
| Approval A0 | The owner approved the #141 work package (A0) after this record was collected. See the [decision record](../../boot-update-investigation.md#approvals). |
