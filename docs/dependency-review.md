# Dependency review

Status: **procedure from [ADR-0006](decisions/0006-cryptographic-dependency-policy.md) (accepted 14 November 2026)**, written 14 November 2026. First applied in the [review of Mbed TLS 4.1.1 and TF-PSA-Crypto 1.1.1](dependency-review-mbed-tls-4.1.1.md). It is a procedure, not a promise to consumers, and it changes after the first reviews show gaps.

A dependency review is a short written decision, for one pinned dependency and one trigger, on whether a known vulnerability or a newer release matters to what Crux builds, and what Crux does about it. It is not an audit of the dependency. It is the review that [`docs/credential-store.md`](credential-store.md) names where it says "not assessed".

## Scope

**Crypto first:** `mbedtls` and `tf-psa-crypto`, the three wolf projects (`wolfssl`, `wolfssh`, `wolfpsa`), Zephyr's `modules/mbedtls` glue, and Crux's entropy replacement (`scripts/prepare-mbedtls-entropy.py`), which depends on the pin.

**Other dependencies that would need the same review.** FatFs (the first open case), littlefs, Reliance Edge, and the wolf projects once the Wolf profile moves. `hal_nxp` and MCUboot are candidates too. This guide decides none of them: each is the owner's decision, and the order recommended in ADR-0006 puts FatFs after the crypto pair.

## When to run it

- An upstream security release or advisory for a pinned dependency.
- A Zephyr tag or branch that moves a pin.
- A change of Crux's configuration of a crypto feature, which can turn an old "not applicable" into "applies".
- A consumer report.
- An open "not assessed" line in `docs/`.

Target: a recorded review within 14 days of a trigger, as a Crux target and not a promise to consumers (ADR-0006, Q4).

## What the record holds

| Field | Content |
| --- | --- |
| Subject | The dependency, the pinned revision and its upstream base, the candidate revision, the upstream releases and their dates |
| Sources | Each upstream advisory read, with URL and the date read |
| Configuration read | What Crux compiles and calls, with `file:line` |
| Per advisory | What it needs, what the Crux evidence shows, **applies** (yes, no, cannot tell), and the verdict |
| Fix inclusion | Whether the candidate revision holds each listed fix, and how that was checked |
| What could not be established | Every claim without evidence, stated as such |
| Plan or evidence | What a pin move would run, as a plan, or what it ran |
| Reviewer, date, model | Who wrote it, who checked it, and when |

Record it where the work is tracked: in the PR that moves the pin or in the issue, and as a file under `docs/` when it should outlast the PR, as the [first record](dependency-review-mbed-tls-4.1.1.md) does. Label each claim **Verified**, **Reported**, **Inference** or **Not verified**, as [ADR-0004](decisions/0004-product-composition-of-management-services.md) does.

## Steps

1. **List the advisories** fixed between the pinned base and the candidate: the release pages and the upstream advisory files. Read each text for its affected versions, conditions, work-around and fix commits.
2. **Read Crux's configuration** for the profile concerned. For the Mbed profile that is `tests/native/tls_offload_probe/mbed.conf`, `tests/native/management_composition/profile-psa.conf`, the web adapter's listener setup (`components/web/ports/zephyr/web_http_server.c`), the PSA call sites under `components/*/ports/psa/`, and Zephyr's Kconfig and configuration header at Crux's Zephyr tag (`modules/mbedtls/Kconfig.mbedtls`, `Kconfig.tf-psa-crypto`, `configs/config-mbedtls.h`, `configs/config-tf-psa-crypto.h`, `subsys/net/lib/sockets/sockets_tls.c`). The resolved `.config` of the build is the source of truth. Where none is in the repository, say so, and name the symbols a CI log should show.
3. **For each advisory decide:** **applies** when the configuration compiles the affected feature and Crux reaches it, **no** when the evidence shows it does not, and **cannot tell** when it does not settle the question, with what would settle it. State what would make a "no" false.
4. **Check the fix inclusion** for the candidate (below).
5. **Decide:** **take** (move the pin, with the evidence below), **defer** (with the reason and a date or event to revisit), or **not applicable** (with the evidence of step 3). A "cannot tell" is a take or a defer, never a silent "not applicable".
6. **Record it**, with the unestablished points, and have the `reviewer` agent check the record against the advisories and the configuration. Its PASS is not an approval of the pin move.
7. **A pin move needs the owner's approval of the move itself.** After the first two moves, ADR-0006 (Q3) allows reviewer PASS plus green CI for a move within the same upstream line with unchanged results.

## Check what a pin is

A Zephyr module pin is an upstream release tag plus Zephyr's commits, not the tag. The check below finds the base and the number of extra commits. It uses public GitHub and needs `gh`.

```bash
gh api 'repos/zephyrproject-rtos/mbedtls/contents/include/mbedtls/build_info.h?ref=<pin>' --jq .content | base64 -d | grep VERSION_STRING
```

```bash
gh api repos/Mbed-TLS/mbedtls/tags --jq '.[] | select(.name=="mbedtls-4.1.1") | .commit.sha'
```

```bash
gh api repos/zephyrproject-rtos/mbedtls/compare/<upstream tag commit>...<pin> --jq '{status,ahead_by,behind_by}'
```

`behind_by` 0 means the pin holds every commit of the upstream tag. `ahead_by` is the number of Zephyr commits on top, and their subjects (`--jq '.commits[].commit.message | split("\n")[0]'`) show what they are: `[zep noup]` for packaging, `[zep fromlist]` and `[zep fromtree]` for upstream changes. For `tf-psa-crypto` use `include/tf-psa-crypto/build_info.h` (macro `TF_PSA_CRYPTO_VERSION_STRING`) and the repositories `zephyrproject-rtos/tf-psa-crypto` and `Mbed-TLS/tf-psa-crypto`. GitHub resolves a commit across a fork network, so a commit that resolves in the upstream repository is not necessarily on an upstream branch.

In a west workspace the same questions have offline answers: the version macros in `modules/crypto/mbedtls/include/mbedtls/build_info.h`, and `git -C modules/crypto/mbedtls merge-base --is-ancestor <commit> HEAD`.

## Check that a revision holds a fix

Each upstream advisory has a "Fix commits" table by branch. For the 4.1.x column of an Mbed TLS advisory, and the 1.1.x column of a TF-PSA-Crypto one, test each listed hash, and both ends of a range:

```bash
gh api repos/zephyrproject-rtos/<module>/compare/<fix commit>...<revision> --jq .status
```

`ahead` or `identical` means the fix commit is an ancestor of the revision. `diverged` or `behind` means it is not. A failure to resolve is not a "no": say that the hash could not be tested. Do not infer inclusion from the date of the revision's commit. This shows the commit is present, not that the fix is complete or works in Crux's build, and it tests range ends only, not every commit inside a range.

## What a pin move runs

[ADR-0006 option B item 4](decisions/0006-cryptographic-dependency-policy.md#3-policy-options): the pin consistency check, the Host and `native_sim` PSA conformance, and the Mbed-profile HTTPS composition on `native_sim`, with RT1060 compile steps, and the resolved `.config` read. The `psa-crypto` workflow already runs these on every pull request (`.github/workflows/psa-crypto.yml`). A physical-board run is not required, and an earlier board result does not carry over to a new pin. [The first review](dependency-review-mbed-tls-4.1.1.md#plan-for-the-pin-move-evidence) has the step list.

## What a consumer sees

- **The pin base check above.** It answers "which line and version does this Crux revision carry" for any consumer with `gh` or a workspace.
- **A release notes line** whenever a PR moves a crypto pin: the version before and after, the advisory identifiers, and a link to the review record. The line is written by hand until a generator carries it (ADR-0006, Q6).
- **`west crux-pins`** reports a mismatch with Crux's pin, not the line or version.
- **The compatibility table** ([compatibility table](compatibility-table.md)) shows the commit, and shows a version only when `west.yml` carries a version comment, which the generator does not check against the commit.

What a product can and must do about the pins is in [Crypto library pins](product-composition.md#crypto-library-pins).
