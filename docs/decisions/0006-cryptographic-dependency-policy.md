# ADR-0006: How Crux handles its cryptographic dependencies over time (proposal)

Status: **Proposed**. Baseline: `origin/main` at `8a6fc5a59142963ebe49d6d0041ad29c952f47d4`. Author: Claude Code, model `claude-sonnet-5-5`. The owner approved writing this proposal, and only the proposal, in the Crux Pilot main session chat on 14 November 2026 at about 18:37 UTC (relayed by that session as the words "추천대로 승인할게", meaning "I approve as you recommended", for the item it recommended: a short crypto dependency policy, to be written after the phase 0 and System proposals and allowed to start now). The question behind it came from the owner through the Crux Controller session: is wolfSSL used, and what does Crux do about Mbed TLS support ending in 2027?

This record is documentation only. It adds no code, check, target or Kconfig symbol, changes no `west.yml` pin, changes no protected path and opens no GitHub issue. It adopts nothing. Every change it puts forward would be its own reviewed change, listed under [Follow-up changes](#follow-up-changes). The number 0006 is used because ADR-0005 is taken by the open Draft PR #404 (**Verified** with `gh pr list` on 14 November 2026).

**Labels.** **Verified** means I read it at the baseline revision and cite `path:line`, or I read it from an upstream repository with `gh api` on 14 November 2026 and cite the URL. **Reported** means a named document says it and I did not re-check it. **Inference** is my reading. **Not verified** means no run or read exists. No build, `native_sim` run, `west` command or test ran for this record: see [What was run](#what-was-run).

## The recommendation in brief

- **The 2027 date is not Crux's exposure.** Mbed TLS 3.6 LTS ends in March 2027, but Crux is on the 4.1 line, which upstream supports "until at least March 2029". The real gap is different, see the next point.
- **The real gap: Crux's two Mbed TLS pins predate a security release.** At the baseline, `mbedtls` is upstream 4.1.0 plus 8 Zephyr commits and `tf-psa-crypto` is upstream 1.1.0 plus 10 Zephyr commits. Upstream released 4.1.1 and 1.1.1 on 7 July 2026 with fixes for 13 and 5 named security advisories. Zephyr put the fixed revisions on its `v4.4-branch` on 13 August 2026, six days after v4.4.2, and as of 14 November 2026 no v4.4.3 tag exists. Crux pins the v4.4.2 tag, so it carries neither fix set, 130 days after the upstream release. Whether any of those advisories matters for Crux's configuration was **not assessed**: no document defines how.
- **No document records a crypto policy for Crux's own pins.** Nothing says how long Crux stays on a pinned crypto commit, how a security fix is taken, who reviews a pin move or how a consumer learns that one moved the crypto line. The product side is covered (`docs/product-composition.md:326-340`: the product's owner decides). Section 1 has the evidence.
- **Recommended policy: B.** Follow Zephyr's revisions at each baseline (A), plus a defined security-fix path that may move only the two Mbed TLS pins ahead of the Zephyr tag, only to revisions that Zephyr itself pins on a release branch, after a recorded dependency review. **Alternative: A**, if the owner prefers to carry no exception machinery until the first release tag. **C**, owning the pin independently of Zephyr, is not recommended.
- **The dependency review that `docs/credential-store.md:242` already names is defined here** (section 5), crypto first, and the review is where "not assessed" becomes an answer.
- **Nothing in the licence boundary is decided.** The terms of the wolfSSL TLS socket patch stay unconfirmed (section 4).
- **Eight questions** for the owner are at the end, each with a recommendation.

## 1. Current state

| Fact | Evidence | Status |
| --- | --- | --- |
| `mbedtls` is pinned at `a3e190fe44c78d1ba67f55979e1257328cc7d0d8` and `tf-psa-crypto` at `dc575a2ddcc8cb16275d24c42a52eaf79ebe2231`, both from Zephyr's forks, both in the group `crux-secure-management`. The comment says they follow Zephyr's own `west.yml` at the Zephyr tag (v4.4.2) | `west.yml:27-39`, `west.yml:19` | Verified |
| The group is off by default, so an ordinary workspace fetches no crypto library and no GPL stack | `west.yml:3-7` | Verified |
| The version macros at those commits read Mbed TLS 4.1.0 and TF-PSA-Crypto 1.1.0 | `include/mbedtls/build_info.h` at `a3e190fe` in `zephyrproject-rtos/mbedtls`, and `include/tf-psa-crypto/build_info.h` at `dc575a2d` in `zephyrproject-rtos/tf-psa-crypto`, read with the GitHub contents API on 14 November 2026 | Verified |
| Build evidence has `CONFIG_MBEDTLS_VERSION_4_x=y`, so the builds use the 4.x line and not 3.6 | `docs/evidence/rt1060-management-image-feasibility/integrated-vehicle/config.txt:309` (and eight sibling files in that folder) | Verified |
| Crux's manifest imports only `cmsis`, `cmsis_6`, `hal_nxp`, `littlefs` and `fatfs` from Zephyr, so Zephyr's `mbedtls-3.6` project (kept in Zephyr's manifest for TF-M) is not in a Crux workspace | `west.yml:20-26`, and Zephyr `west.yml` at v4.4.2, `mbedtls-3.6` entry with the comment "Required for TF-M build until we bump it to v2.3" | Verified |
| wolfSSL `v5.9.4-stable`, wolfSSH `v1.6.0-stable` and wolfPSA `v5.9.4` are pinned by tag in the same opt-in group. They serve the Wolf profile, which SSH and SFTP need. GPL-3.0-or-later or commercial | `west.yml:40-58`, `docs/management-in-a-product.md` (SSH), `patches/zephyr/README.md` (licence) | Verified for the pins, Reported for the licence terms |
| Zephyr's v4.4.0, v4.4.1 and v4.4.2 manifests all pin the same two commits, so the baseline move of #199 (v4.4.0 to v4.4.2) did not change a crypto pin. Crux began to pin them in #202, after #199 | Zephyr `west.yml` at the three tags (GitHub contents API, 14 November 2026). `git log -- west.yml`: `5650ffd0` (#202) is newer than `ac5a348d` (#199) | Verified |
| Whether the pins equal the upstream release tags is open in the repository's own record | `docs/psa-crypto-qualification.md:147` (finding 6) | Verified. Settled in section 2 |
| The Host build fetches TF-PSA-Crypto as a pinned GitHub archive with a SHA-256, at the same revision as `west.yml` | `cmake/CruxPsaCrypto.cmake:10-12` and `:21-26` | Verified |
| `scripts/test-psa-crypto.sh` fails if the revision in `cmake/CruxPsaCrypto.cmake`, in `west.yml`, in **Zephyr's own manifest** and in the checkout are not the same, for both modules | `scripts/test-psa-crypto.sh:87-105` | Verified by reading, not run |
| The crypto evidence is tied to those commits: the qualification records name them and the Zephyr v4.4.2 builds | `docs/psa-crypto-qualification.md`, `docs/evidence/psa-crypto/v4.4.2-revised/` | Reported |
| `west crux-pins` merges Zephyr's pins with Crux's, "explicit Crux entries win over Zephyr's". A Crux pin ahead of Zephyr's tag is therefore the contract and reports `OK`, not `MISMATCH` | `scripts/west_commands/crux_pins.py:66-75` | Verified by reading, not run |
| The baseline gate compares only Zephyr's release numbers. A crypto pin is not covered | `docs/product-composition.md:161-169` | Verified |
| The rule for a Zephyr patch release is: `west crux-pins` reports `MISMATCH`, the product moves the Zephyr revision and adds a `crux-override` for each moved module | `docs/product-composition.md:346-355` | Verified |
| A FatFs range-check CVE is listed as "Not assessed. It is for dependency review", and no document defines that review | `docs/credential-store.md:242` | Verified |
| Zephyr's TLS socket entropy glue (`modules/mbedtls/zephyr_entropy.c`) is replaced by a Crux copy, checked against the SHA-256 of the Zephyr source at the pin | `scripts/prepare-mbedtls-entropy.py:9-13`, `patches/zephyr/mbedtls-entropy-fail-closed.patch` | Verified |

| A **product's** process for an advisory or upstream release is recorded: the product's named owner decides, a released product has a maintenance branch, and each product sets its own support period. The guide gives no number. It says nothing about Crux's own pins | `docs/product-composition.md:326-340` ("Upgrades and security fixes") | Verified |
| `docs/baseline.md:1239` is titled "Module header and dependency review". It reviews header ownership and include paths, not vulnerabilities, so it is not the review that `docs/credential-store.md:242` names | `docs/baseline.md:1239-1257` | Verified |

**What is missing.** The nearest statement about Crux's own pins is the implicit rule of the comment at `west.yml:27`. No document records any of the following: how long Crux stays on a pinned crypto commit, how Crux takes a security fix for a pinned dependency, who reviews a pin move, how a consumer learns that a pin moved the crypto line, or what the "dependency review" of `docs/credential-store.md:242` consists of (**Verified** by `grep` of the maintained `docs/` (not `plans/`, `history/`, `evidence/` or `decisions/`) for "dependency review", "security advisor", "security fix" and "CVE", which found only the three places in the table above, and by reading `docs/product-composition.md`).

## 2. Upstream facts

All rows were read on 14 November 2026. The upstream repositories are public and I used only `gh api` and `curl` on public URLs. No repository content or private context was sent.

### 2.1 What upstream supports

| Fact | Source | Status |
| --- | --- | --- |
| Maintained branches of Mbed TLS: `main`, `development`, and the LTS branches `mbedtls-3.6` and `mbedtls-4.1`. LTS branches get bug fixes and security fixes | <https://github.com/Mbed-TLS/mbedtls/blob/main/BRANCHES.md> | Verified |
| "Mbed TLS 3.6 LTS (released in March 2024) will be supported until March 2027." The first 4.x LTS, 4.1, was released in March 2026 "and will be supported until March 2029" | same file | Verified |
| The 4.1.1 release notes say "Mbed TLS 4.1 is a long-term support (LTS) branch. It will be supported with bug-fixes and security fixes until at least March 2029" | <https://github.com/Mbed-TLS/mbedtls/releases/tag/mbedtls-4.1.1> | Verified |
| TF-PSA-Crypto keeps `tf-psa-crypto-1.1` "maintained until March 2029". It says that `mbedtls-3.6` holds the PSA implementation from before the repository split, and that "Mbed TLS LTS branches created after the repository split, such as mbedtls-4.1" are not covered by its backport note | <https://github.com/Mbed-TLS/TF-PSA-Crypto/blob/main/BRANCHES.md> | Verified |
| Released 31 March 2026: Mbed TLS 4.1.0 and TF-PSA-Crypto 1.1.0. Released 7 July 2026: Mbed TLS 4.1.1 and 4.2.0, TF-PSA-Crypto 1.1.1 and 1.2.0 | the release pages of those tags, `published_at` field of the GitHub releases API | Verified |
| The 4.1.1 release lists 13 security advisories (TLS 1.2 and 1.3 handshake, session ticket, X.509 CA bit, signature algorithm restrictions, PKCS7, the random generator fault, among others). The TF-PSA-Crypto 1.1.1 release lists 5 (RSA PKCS#1 v1.5 timing, ChaCha20 counter overflow, ECC optimized modp side channel, Everest low-order input, a zero-length ECC public key read) | the two release pages, advisory texts under <https://github.com/Mbed-TLS/mbedtls-docs/tree/main/security-advisories> | Verified for the lists. **Not assessed:** whether each applies to Crux's configuration |

**Reading for Crux.** Crux is on the 4.1 line (4.1.0 and 1.1.0), which is the LTS line (**Inference**, from the version macros and the branch statement). The March 2027 end concerns 3.6, which no Crux build uses. A future change of the line, such as a move to 4.2 (non-LTS, **Inference**: BRANCHES.md names no 4.2 LTS), is a separate decision and is not proposed.

### 2.2 How Zephyr's commits relate to the upstream tags

| Fact | How established | Status |
| --- | --- | --- |
| Crux's `mbedtls` pin is upstream's `v4.1.0` commit (`0fe989b6b514192783c469039edd325fd0989806`) plus 8 commits. `ahead_by` 8, `behind_by` 0 | `gh api repos/zephyrproject-rtos/mbedtls/compare/0fe989b6...a3e190fe`. The Zephyr repository holds the upstream history, so the compare resolves inside it | Verified |
| Those 8 commits are `[zep noup]` packaging (remove submodules, check in generated files, add `module.yml`, document maintenance, a generic path for TF-PSA-Crypto, a CMake minimum, delete a PR template) and one `[zep fromlist]` change that removes an RSA encryption requirement from the ECDHE-RSA configuration check | commit subjects from the same compare | Verified for the subjects. **Not verified:** the diffs |
| Crux's `tf-psa-crypto` pin is upstream's `v1.1.0` commit (`29160dd877d29658279fd683b2ae57b320ddcf09`) plus 10 commits: two `[zep fromlist]` build-warning fixes, one `[zep fromlist]` documentation-path change, and `[zep noup]` packaging | `gh api repos/Mbed-TLS/tf-psa-crypto/compare/29160dd8...dc575a2d` (`ahead_by` 10, `behind_by` 0) | Verified for the counts and subjects |
| GitHub resolves a commit across a fork network, so resolving the `tf-psa-crypto` pin in `Mbed-TLS/tf-psa-crypto` does **not** show that the commit is on an upstream branch. `zephyrproject-rtos/mbedtls` is reported by the API as not a fork (`parent` null), and the pin does not resolve in `Mbed-TLS/mbedtls` (HTTP 422) | `gh api` on both | Verified |
| The pins are therefore an upstream release tag plus Zephyr packaging and two or three small fixes, **not** upstream tags. A consumer cannot take the upstream tag instead without losing Zephyr's packaging | the rows above | Inference |
| Zephyr's documented process for the fork: patches on top of upstream releases carry `[zep fromtree]`, `[zep fromlist]` or `[zep noup]`. A version bump makes a branch `zephyr_<project>_v<upstream version>` at the upstream tag and cherry-picks the extra patches onto it; the new branch becomes the default | <https://github.com/zephyrproject-rtos/mbedtls/blob/a3e190fe44c78d1ba67f55979e1257328cc7d0d8/README.md> (sections "Additional patches" and "Version bumping") | Verified |

This answers the open point of `docs/psa-crypto-qualification.md:147`, with the limit that I compared commits and read subjects, and did not diff file contents. **Not verified:** that the build input is byte-identical to what upstream tests, which no one can claim with 8 to 10 extra commits.

### 2.3 How Zephyr updates these modules and handles security fixes

| Fact | Source | Status |
| --- | --- | --- |
| Zephyr's v4.4.0 (14 April 2026), v4.4.1 (10 June) and v4.4.2 (7 August) all pin `mbedtls` `a3e190fe` and `tf-psa-crypto` `dc575a2d`. Its v4.5.0 (28 October) pins `c43f34b9` (the fork tag `zephyr_mbedtls_v4.1.1`) and `765af96a` (the fork tag `zephyr_tf-psa-crypto_v1.1.1`) | Zephyr `west.yml` at each tag, release dates from the releases API | Verified |
| `v4.4-branch` has the commit "manifest: update Mbed TLS to v4.1.1/v3.6.7 and TF-PSA-Crypto to v1.1.1" (`9b9a84fc86954527322fdfb3e05628c5b3928a64`). Authored 16 July 2026, committed 13 August 2026. It changes only `west.yml`, to `mbedtls` `098e120a` and `tf-psa-crypto` `fa923495`. `v4.4.2` is its ancestor (`ahead_by` 9 from v4.4.2) | the commit and `compare/v4.4.2...9b9a84fc8` in the Zephyr repository | Verified |
| `098e120a` is upstream `v4.1.1` plus 8 commits (7 `[zep noup]` packaging and `[zep noup] compile TF-PSA-Crypto only when !CONFIG_TF_PSA_CRYPTO_CUSTOM`). `fa923495` is upstream's `v1.1.1` plus 24 commits. The version macros read 4.1.1 and 1.1.1 | compares in the two fork repositories and upstream, `build_info.h` at the commits | Verified for the counts and the macros. **Not verified:** the content of the 24 commits |
| No `v4.4.3` tag exists on 14 November 2026. The three tags of the 4.4 line came 57 and 58 days apart. The gap since v4.4.2 is 99 days, and the crypto update has sat untagged on `v4.4-branch` for 93 days | `git/matching-refs/tags/v4.4` lists v4.4.0 to v4.4.2 and their rc tags only | Verified for the tag list. The 57 and 58 days are my arithmetic from the release dates |
| Zephyr's `doc/releases/index.rst` lists releases 3.7 and 4.4 to 4.5 in its table of supported releases. 4.6 is planned for April 2027 | <https://github.com/zephyrproject-rtos/zephyr/blob/main/doc/releases/index.rst> | Reported. The table itself is generated and I did not render it |
| Zephyr's security policy: each security issue fixed in Zephyr is backported to the current LTS release and the most recent two releases. Each release lists its CVEs. Embargo up to 90 days | <https://github.com/zephyrproject-rtos/zephyr/blob/main/doc/security/reporting.rst> | Verified. It speaks of issues in Zephyr |
| I found **no statement** in Zephyr's security documents (`reporting.rst`, `security-overview.rst`, `vulnerabilities.rst`, read through `gh api` and `grep`) on how an advisory in a third-party module such as Mbed TLS is handled between tags. The practice shown by the history is a `west.yml`-only commit on the release branch, with no tag | the same files, and the commit above | Verified for the absence in those three files. **Not verified:** other places, such as a wiki or mailing list |
| The `v4.4-branch` has 401 commits after v4.4.2. Of the files that the compare returned (the API caps the list), the ones matching `mbedtls`, `psa` or `tls` are `drivers/crypto/crypto_mbedtls_shim.c`, `modules/mbedtls/Kconfig.tf-psa-crypto` and `modules/openthread/platform/crypto_psa.c`. `modules/mbedtls/zephyr_entropy.c`, which Crux patches, is not among them | `compare/v4.4.2...v4.4-branch` | Verified within the cap. **Not verified:** a complete file list |

**Practical effect of the baseline moves already made.** #199 moved v4.4.0 to v4.4.2 and changed no crypto pin, because Zephyr's patch tags did not touch them. The crypto pins moved in Zephyr only by a post-tag commit (4.1.1) and by the minor release v4.5.0. So under the rule "follow Zephyr's tag", Crux gets a crypto security fix either when Zephyr tags v4.4.3 (no date) or when Crux moves to v4.5.0. The second is a baseline move with its own cost: the wolfSSL TLS socket patch is generated for Zephyr 4.4.1 and needs `git apply --3way` on 4.4.2 (`patches/zephyr/README.md`), and Crux's evidence is tied to v4.4.2 (`docs/product-composition.md:346-355`). **Not verified:** that v4.5.0 builds with Crux.

### 2.4 The wolfSSL side

| Fact | Source | Status |
| --- | --- | --- |
| The latest GitHub releases on 14 November 2026: wolfSSL `v5.9.4-stable` (27 September 2026), wolfSSH `v1.6.0-stable` (6 October), and the latest wolfPSA tag `v5.9.4`. These are the tags Crux pins | releases API and tags API of `wolfSSL/wolfssl`, `wolfSSL/wolfssh` and `wolfSSL/wolfPSA` | Verified. The Wolf pins are at the latest tag |
| wolfSSL's earlier releases this year: 5.9.0 (19 March), 5.9.1 (8 April), 5.9.2 (25 June). A 5.9.3 is not among the first six releases listed | releases API | Verified. I did not look for a reason |
| The 5.9.4 release notes list vulnerabilities fixed, and wolfSSL's vulnerability page lists several High items fixed in 5.9.4 (an OCSP stapling flaw, a raw public key flaw and others). Crux's pin includes them | <https://github.com/wolfSSL/wolfssl/releases/tag/v5.9.4-stable>, <https://www.wolfssl.com/docs/security-vulnerabilities/> | Verified in part: I read the head of the release notes and the part of the page around 5.9.4. **Not assessed:** their relevance |
| Licence: the `LICENSING` file says GPLv3 with an exception for named software. The Zephyr port repository `wolfSSL/osp` says GPL-3.0-or-later or a standard commercial licence, and that other projects in it follow their own licences. wolfPSA's licence in the GitHub API is GPL-3.0 | <https://github.com/wolfSSL/wolfssl/blob/master/LICENSING>, <https://github.com/wolfSSL/osp/blob/master/README.md>, repository metadata | Verified for what the files say. The commercial terms are the vendor's and were not read |
| A support or end-of-life statement for wolfSSL stable branches | I looked in the repository README and `SECURITY` paths and found none | **Not verified.** The vendor may state one elsewhere |

**Reading for Crux.** wolfSSL has no Zephyr-style coupling: Crux pins the tag directly and can move it without a Zephyr change. The wolf projects need the same review as Mbed TLS (section 5), and their pins are in the opt-in group only.

## 3. Policy options

All three keep the Crux manifest's rule that crypto libraries live in the opt-in group `crux-secure-management`, and keep the wolf pins on upstream tags. They differ in how the two Mbed TLS pins move.

**Option A, follow Zephyr's module revisions at each Zephyr baseline, and nothing else.** The two pins change only when Crux's Zephyr baseline moves, to the revisions that Zephyr's `west.yml` holds at the new tag. Between baselines Crux takes no crypto fix.

**Option B, A plus a defined security-fix path.** A pin may move ahead of the Zephyr tag, under these conditions. They are a proposal for the owner, not adopted rules.

1. *Allowed target.* Only a revision that Zephyr itself pins on a release branch or on `main`, so that Zephyr's review and packaging apply. If Zephyr has no such revision, the move is not allowed under this path and goes to the owner as a separate decision.
2. *Trigger.* An upstream security release or advisory for the line Crux builds (Mbed TLS 4.1, TF-PSA-Crypto 1.1), a Zephyr tag that moves a crypto pin, or a report from a consumer. The wolf pins use the same triggers on the wolf side.
3. *Review first.* A recorded dependency review (section 5). The review, not the trigger, decides take, defer or not applicable.
4. *Evidence.* The pin consistency check, the Host and `native_sim` PSA conformance runs, and the Mbed-profile HTTPS composition on `native_sim` run on the new pin, with results recorded in the PR. A physical-board run is not required unless the owner asks, and a board result does not carry over from the old pin (`AGENTS.md`: distinguish Host, `native_sim`, CI and physical evidence).
5. *Review and approval.* The `reviewer` agent's PASS, then the owner's approval of the pin move. Later moves within the same upstream line with unchanged results could move to "reviewer PASS and green CI" if the owner delegates it (question Q3).
6. *Visibility.* A pin move is stated in the PR and the release notes line of section 6.
7. *Timing.* A recommended target, not a promise to consumers: the review is recorded within 14 days of the trigger (Q4).

**Option C, own the crypto pin independently of Zephyr.** Crux pins the crypto modules to commits it chooses, such as upstream release tags, with no tie to Zephyr's manifest. Crux owns the choice, the packaging and the integration with Zephyr's `modules/mbedtls` glue.

| Question | A | B | C |
| --- | --- | --- | --- |
| Lag behind an upstream security release | Unbounded, set by Zephyr's tag cadence. Today 130 days from the upstream release | Bounded by the review target. The first move would be an immediate one | Bounded by Crux |
| Who decides the timing | Zephyr, and Crux's baseline decision | Crux, per trigger | Crux |
| Is the combination tested by anyone else | Zephyr's CI ran the tag's pair (**Reported**: I did not read Zephyr's CI) | The `v4.4-branch` pair is Zephyr's merged state. I did not check that Zephyr's CI covered it | No: upstream tags lack the Zephyr packaging commits, so Crux builds and integrates alone |
| Match with Zephyr's glue (`modules/mbedtls`, the Kconfig) | By construction | By construction for a Zephyr-pinned revision | Crux's risk. Zephyr's own `[zep noup]` commit "compile TF-PSA-Crypto only when !CONFIG_TF_PSA_CRYPTO_CUSTOM" is the kind of coupling at issue |
| Protected-path changes needed | None | Yes: `scripts/test-psa-crypto.sh` rejects a pin that differs from Zephyr's manifest (`:93-97`) | Yes, the same, and more |
| Evidence cost per move | One per baseline, already part of a baseline move | One per pin move: a small set (item 4) | The same, plus the integration |
| What a consumer sees | A pin per Crux revision, as now | A pin per Crux revision, with a release notes line | The same |
| Licence | Unchanged (Apache-2.0 OR GPL-2.0-or-later for the Mbed TLS pair, **Reported** in `patches/zephyr/README.md`) | Unchanged | Unchanged |
| Reversibility | Easy | Easy: a pin move is a one-line manifest change | Harder: Crux owns what it packaged |

**Recommendation: B.** The numbers in section 2.3 are its reason: under A, a crypto security release reaches Crux only at a Zephyr tag that has no date, and the current state is 93 days of fix on Zephyr's release branch that Crux cannot take without leaving its pin rule. B keeps A's rhythm, takes only revisions that Zephyr has chosen, and gives the exception a name and a record. Its cost is one check script change (protected) and a review for each move. **Alternative: A**, if the owner judges that, with no release tag and no shipped product (`docs/product-composition.md` says no tag exists), the exception machinery should wait. Under A, the policy would still need the dependency review of section 5 and a rule to re-evaluate the pins before the first release tag. **C is not recommended.** It makes Crux the maintainer of a combination that nobody else tests, to gain control that B already gives for the two pins.

### What a consumer product can and must do

**Can.** Take a Crux revision as it is. Move a crypto pin in its own manifest, for example to the `v4.4-branch` revisions, and declare it with `crux-override`, so that `west crux-pins` reports `OVERRIDE` with the reason (`docs/product-composition.md:106`, `:284`, `:346-355`). The product then owns the evidence for that pin, because Crux's evidence is tied to Crux's pin. **Inference:** the override mechanism works for these two modules the same way it does for Zephyr's, because the contract merge treats them alike (`crux_pins.py:66-75`). I did not run it.

**Must.** Keep `mbedtls` and `tf-psa-crypto` from one line, as Zephyr pins them together (**Inference**: every Zephyr revision read pairs a 4.1.x Mbed TLS with a 1.1.x TF-PSA-Crypto). Run its own dependency review for what it ships, because the review of this record covers Crux's configuration and not a product's. Record which crypto line and version a released product carries (section 6). Meet the licence terms of any group it enables (section 4). Name one owner for the product's own advisories and upgrades, as `docs/product-composition.md:337-339` already asks. Not rely on Crux's physical evidence for a different pin.

## 4. The licence boundary as a constraint

These are constraints, not decisions, and this record decides none of them.

- wolfSSL and wolfSSH are offered under GPL-3.0-or-later or a commercial licence, and wolfPSA's source states GPL-3.0-or-later only (`patches/zephyr/README.md`, **Reported**; the files read in section 2.4 agree). A binary built with them is subject to those terms when distributed.
- The terms of the wolfSSL TLS socket patch are unconfirmed. The upstream repository says that ports of other projects follow each project's licence, and it does not name the patch, which also calls wolfSSL (`patches/zephyr/README.md`). Until they are confirmed, the Wolf profile cannot be carried in a public product (`docs/decisions/0004-product-composition-of-management-services.md`, phasing).
- Mbed TLS and TF-PSA-Crypto are Apache-2.0 OR GPL-2.0-or-later (**Reported**, same README). A pin move within them does not change that.
- The wolfSSL patch is generated for Zephyr 4.4.1 and needs a 3-way merge on 4.4.2. **Any option that moves the Zephyr baseline therefore touches the Wolf profile's patch**, without changing its terms.

What follows for this policy: the review and pin rules apply to the wolf group, but a wolf pin move is licence-neutral, and the policy states no rule about patch terms, which stay with the owner's open decision (Issue #185 and ADR-0004 C8).

## 5. The dependency review

`docs/credential-store.md:242` says "Not assessed. It is for dependency review" for a FatFs CVE, and no document defines the review. This section defines it. It is a proposal, and `docs/credential-store.md` stays as it is.

**What it is.** A short, written decision, for one dependency and one trigger, on whether a known vulnerability or a newer release matters to what Crux builds, and what Crux does about it. It is not an audit of the dependency.

**Triggers.** An upstream security advisory or security release for a pinned dependency, a new tag of a dependency that a Zephyr or Crux baseline would take, a change of Crux's configuration of a crypto feature, a consumer report, and the entries of `docs/` that already say "not assessed" (the FatFs line is the first).

**Inputs.** The advisory text and its fixed versions, the pin and its upstream base (the check of section 6), the Crux configuration that is built (Kconfig symbols and `PSA_WANT` or `MBEDTLS_*` options of the Mbed profile, the wolf settings for the Wolf profile), which Crux components use the affected function or feature, and the evidence that exists for the pin.

**Steps.**

1. List the advisories fixed between the pinned base and the candidate (for the crypto pair, the release pages of section 2.1).
2. For each, say whether the affected feature is compiled in the configurations Crux ships or tests, and whether Crux calls it. Use the three answers **applies**, **does not apply** (with the reason, such as "client-side only, Crux runs a server"), and **cannot tell** (with what would settle it).
3. Decide: **take** (move the pin, with the evidence of option B item 4), **defer** (with the reason and a date or event to revisit), or **not applicable**. A "cannot tell" is a defer, never a silent "not applicable".
4. Record it where the work is tracked, in the PR that moves the pin or in the issue. The record names the revision reviewed, the advisories with a verdict each, the decision, the reviewer and the date.
5. The `reviewer` agent checks the record against the advisories and the configuration, as `AGENTS.md` asks for a material review. Its PASS is not an approval of the pin move.

**Scope: crypto first.** The Mbed TLS pair and the three wolf projects, plus Zephyr's `modules/mbedtls` glue and the Crux entropy replacement of `scripts/prepare-mbedtls-entropy.py`, which depend on the pin.

**Which other dependencies would need the same, without deciding them.** FatFs (the CVE of `docs/credential-store.md:242` is a first review), littlefs, Reliance Edge (GPLv2 only, opt-in group `crux-reliance`), and the wolf projects once the Wolf profile moves. Also candidates that the task did not list, from the manifest and the open pull requests: `hal_nxp`, and MCUboot (pinned by the open PR #339). Zephyr itself has its own process (section 2.3) and is a candidate for a "what did the baseline move change" review, which `docs/product-composition.md:346-355` partly covers. Each is a decision for the owner.

**First review, not performed here.** The 13 and 5 advisories of the 4.1.1 and 1.1.1 releases are the first input. A reading of the titles only (**Inference**, not the review): Crux's Mbed profile runs an HTTPS server, so server-side TLS 1.2 and 1.3 handshake, X.509 certificate handling and session handling and the PSA random generator fall in scope for the advisories that name them, and client-only paths (an ECDHE-PSK client handshake, a client accepting a HelloRetryRequest) look not to apply. Which TLS versions and key exchanges the Mbed profile enables, and whether Crux calls PKCS7, was **not read**. The result of the review is the owner's input for question Q2.

## 6. How a consumer sees the crypto line, and who reviews a pin move

**What exists.** `docs/compatibility-table.md` documents a generated table of the pinned revisions. A version shows only if the `revision:` line of `west.yml` ends with a comment that looks like a version, and the generator does not check it against the commit. The two Mbed TLS lines have no such comment, so the table would show commits without a version (**Verified** by reading the generator's documentation and `west.yml:28-39`; I did not run the generator).

**Proposed, for the owner (Q6).**

1. **A documented pin base check**, in a guide, with no code. For a pin, find its upstream base and the count of extra commits: `gh api repos/zephyrproject-rtos/<module>/compare/<upstream tag commit>...<pin>` returns `ahead_by` and `behind_by`, and the version macros in `build_info.h` at the pin give the version string. Section 2.2 is the worked example.
2. **A version table for the crypto line**, generated and not written by hand, as the owner decided for the compatibility table (`docs/compatibility-table.md`): Mbed TLS version and TF-PSA-Crypto version of each Crux revision, plus the line (4.1 LTS) and its support end date. This needs a generator change, which is a protected path (`scripts/`).
3. **A release notes line**, written whenever a PR moves a crypto pin: from and to version, the advisory identifiers, and the review record. A product that updates Crux sees it in the notes.
4. **`west crux-pins`** already reports a mismatch against the Crux contract. It shows a pin, not a line or a version, so it complements the table.

**Who reviews a pin move.** The author records the dependency review. The `reviewer` agent, which did not write the change, checks the record, the pin and the evidence against `AGENTS.md`. The owner approves the move, as security-sensitive (`AGENTS.md`, "Escalate before adopting"). A routine delegation later is question Q3.

## Follow-up changes

None is part of this record. Each needs its own reviewed change after the owner decides. Protected paths are flagged so that approval can be asked per change.

| Change | Needed by | Protected path |
| --- | --- | --- |
| This record's acceptance or rejection, and the `docs/decisions/README.md` row | all | No |
| A guide for the dependency review (section 5) and the pin base check (section 6), as a new file under `docs/`, and a pointer from `docs/credential-store.md:242` | A, B, C | No |
| A statement in `docs/product-composition.md` that the crypto pins follow Zephyr's tag, and what a consumer does | A, B, C | No |
| `scripts/test-psa-crypto.sh`: accept a declared "security pin" that differs from Zephyr's manifest, naming the allowed revisions | B, C | **Yes**, `scripts/` |
| The pin move itself, `west.yml` and `cmake/CruxPsaCrypto.cmake` (the SHA-256 of the archive for the new revision), for the first exercise of B: `098e120a` and `fa923495` | B | No (`west.yml` and `cmake/CruxPsaCrypto.cmake` are not on the list). Security-sensitive, so the owner's approval |
| New evidence records for the pin move | B, C | New files under `docs/evidence/` may be added within the task. Existing records stay unchanged |
| `scripts/gen-compat-table.py` and its tests: a crypto line column | B (Q6) | **Yes**, `scripts/` |
| `CONTRIBUTING.md`: who reviews a pin move | B (optional) | **Yes** |
| A CI check that reports a pin behind the latest patch release of its upstream line | B (optional) | **Yes**, `.github/`. It needs network access, and is not proposed now |
| Pins to upstream tags, packaging, and re-qualification of the Zephyr glue | C only | `scripts/` and more |

## Open questions for the owner

The recommendation comes first in each row.

| # | Question | Recommendation | Reason, and the alternative |
| --- | --- | --- | --- |
| Q1 | Which policy: A, B or C? | **B** | Section 2.3 shows that under A a crypto security release reaches Crux at an undated Zephyr tag, 130 days after upstream's. B takes only Zephyr-pinned revisions, so Zephyr's packaging stays. Alternative: A, with the review of section 5 and a re-evaluation before the first release tag. C is not recommended |
| Q2 | Should the first pin move, to the `v4.4-branch` revisions (`098e120a`, `fa923495`), be started once this policy is accepted? It needs the dependency review of the 4.1.1 and 1.1.1 advisories first | **Yes, as its own PR after the review** | It is the exercise that shows whether B works, and the lag is already 130 days. It needs the protected change to `scripts/test-psa-crypto.sh` and the evidence runs. Alternative: wait for Zephyr's v4.4.3, with no date, or move the baseline to v4.5.0, which is a larger step (section 2.3) |
| Q3 | Who approves a pin move: the owner for each, or reviewer PASS plus green CI after the first two? | **The owner for each of the first two, then reviewer PASS plus green CI for a move within the same upstream line with unchanged results** | A crypto pin is security-sensitive, and a routine path would save the owner time once the path has run twice. Alternative: the owner always |
| Q4 | A target time for recording a review after a trigger: 14 days? | **14 days, as a Crux target and not a promise to consumers** | It is short enough to bound the lag and long enough for the evidence runs. Alternative: no target, "as soon as practical", which gives nothing to measure against |
| Q5 | Which dependency gets the same review after the crypto pair? | **FatFs, since a CVE is already named and "not assessed"** | It is the concrete open case in `docs/credential-store.md:242`. Alternative: the wolf projects first, because the Wolf profile depends on them. littlefs, Reliance Edge and the others follow |
| Q6 | How should a consumer see the crypto line: a generated crypto-line table and release notes line (needs a protected generator change), or a documented check and release notes line only? | **The documented check and the release notes line now, the generated column later** | The check and the line need no protected change and are enough for the first move. A generated column is the owner's earlier rule for the compatibility table, and can follow. Alternative: version comments in `west.yml`, which the generator reads but never checks against the commit |
| Q7 | Do the wolf pins follow tagged stable releases through the same review, with no coupling to Zephyr? | **Yes** | They are at the latest tags now (section 2.4), and the review applies equally. Their licence terms and the patch stay with the owner's open decision |
| Q8 | Should the consumer rules of section 3 (what a product can and must do) go into `docs/product-composition.md`? | **Yes, as a short section after acceptance** | A product's own review and its choice to override a pin are the consumer's, and the guide is where consumers read. Alternative: leave it in this record only |

## What was run

On native macOS, in the worktree at `8a6fc5a5`: `git fetch`, reads of the files cited above, `grep`, `gh api` and `curl` against public GitHub and wolfSSL URLs (the release, compare, contents, tags and matching-refs endpoints, and the wolfSSL vulnerability page), `gh pr list` and `gh pr view` for #199 and #404. All upstream reads were on 14 November 2026 between about 18:38 and 18:50 UTC. Not run: `west`, the compatibility table generator, `scripts/test-psa-crypto.sh`, any CMake configure, any build, `native_sim`, any test, any hardware command. The documentation link check is recorded in the pull request.

## Record

| Item | Value |
| --- | --- |
| Baseline | `8a6fc5a59142963ebe49d6d0041ad29c952f47d4` |
| Skills and agents used | `ecc:architecture-decision-records` (record format, with this repository's convention taking precedence over its `docs/adr/` default), `ecc:search-first` (search of existing scripts, checks and decisions for what already covers the topic, with no install or dependency). `ecc:deep-research` was not used: it needs firecrawl or exa tools, and I used `gh api` and `curl` on public URLs instead |
| Model | `claude-sonnet-5-5`, Claude Code. This is below what a policy record deserves. The independent review is Opus |
| Independent review | The `reviewer` agent, verdict in the pull request |
| Acceptance | None yet. The record is Proposed |
