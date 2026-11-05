# PSA Crypto qualification for Zephyr and Linux Host

**Status: provider results recorded and decisions adopted under the owner's approval, merged in [PR #153](https://github.com/wonny-labs/crux-pilot/pull/153).** This record supports
[Issue #142](https://github.com/wonny-labs/crux-pilot/issues/142). The design of how consumers reach a provider is in
[the design document](psa-crypto-design.md), and the declared subset is in [the native-free consumer
policy](native-interface-policy.md#qualified-psa-crypto-subset). Nothing here is a claim of general PSA Crypto conformance.

| Approval | Covers |
| --- | --- |
| [Start of #142, 2 November 2026](https://github.com/wonny-labs/crux-pilot/issues/142#issuecomment-6565912192) | Investigation, test-first development, Draft PR, trial builds |
| [#198](https://github.com/wonny-labs/crux-pilot/pull/198), merged 3 November 2026 | The provider decision. Its Licences section covers wolfPSA. |
| [#202](https://github.com/wonny-labs/crux-pilot/pull/202), merged 3 November 2026 | The manifest group, with `mbedtls`, `tf-psa-crypto`, `wolfssl` and `wolfpsa` pinned |
| [Owner decision, 4 November 2026 17:55 UTC](https://github.com/wonny-labs/crux-pilot/issues/183#issuecomment-6637976084) | Literally: PSA as a prerequisite of real HTTPS on `native_sim`, and "the steps that needed asking". **As a reading of that approval:** the Host decision, the subset, and running wolfPSA beyond #142's start approval, which had held it for licence review. The owner did not name these items. Hardware stays excluded. |

## Inputs

| Item | Value |
| --- | --- |
| Zephyr | `dccb09599635bdff17633fa7e9dab014b91dce90`, v4.4.2 |
| `tf-psa-crypto` | `dc575a2ddcc8cb16275d24c42a52eaf79ebe2231`, the same as Zephyr's manifest |
| `mbedtls` module | `a3e190fe44c78d1ba67f55979e1257328cc7d0d8`, the same as Zephyr's manifest |
| `wolfssl` | `3c5eead44904df64e6a5a1f4ebdce377d35a849a`, v5.9.4-stable |
| `wolfpsa` | `55ae0ec58cd2ef4c528f44e8466095550c16ca8a`, v5.9.4 |
| Host toolchain | Clang 18.1.3, CMake 3.28.4 |
| `native_sim` toolchain | Host GCC, as `ZEPHYR_TOOLCHAIN_VARIANT=host` |

`scripts/test-psa-crypto.sh` checks that `cmake/CruxPsaCrypto.cmake`, `west.yml`, Zephyr's manifest and the checkouts agree, and that
the Host archive has the same files as the west checkout.

## Why the earlier `zephyr` CI run failed

Run [41401511997](https://github.com/wonny-labs/crux-pilot/actions/runs/41401511997), 2 November 2026, failed in the step "Fetch pinned Zephyr
workspace and install SDK":

```text
fetch_releases API rate limit exceeded.
Exception: Failed to fetch: 403, {"message":"API rate limit exceeded for 20.172.111.210. ...
```

`west sdk install` looks up SDK releases through the GitHub API without a token, and the runner shared a rate limit. It had nothing to do
with PSA, and no test ran. #164 fixed it on `main` by passing `CRUX_GITHUB_TOKEN`, and this branch has it from the merge of `origin/main`.
After the merge that step passes, in run [41891057049](https://github.com/wonny-labs/crux-pilot/actions/runs/41891057049). The Pull Request has the full result of the latest run.

## Conformance cases

45 cases, in [`psa_cases.h`](../tests/psa_crypto/cases/psa_cases.h). They use `<psa/crypto.h>` and ISO C only, and the composition chooses the provider.
The Zephyr suite adds a check of the list's length. Vectors come from published test values or an independent implementation, as
[`vectors.h`](../tests/psa_crypto/cases/vectors.h) records.

| Area | For | Cases | Negative status checks |
| --- | --- | --- | --- |
| Initialisation, random | Access, store | 3 | Writes only what was asked. Two requests differ. |
| SHA-256 | Device key | 6 | `BAD_STATE`, `BUFFER_TOO_SMALL`, `INVALID_SIGNATURE`, `NOT_SUPPORTED` |
| ECDSA P-256 verify, key identifiers | Image check, and generated keys | 11 | Bad signatures, tampered input, off-curve key, `NOT_PERMITTED`, `INVALID_HANDLE` |
| HMAC-SHA-256 | `crux_access_port_hmac` | 4 | `BUFFER_TOO_SMALL`, `NOT_PERMITTED`. Includes empty input, which an empty login name reaches. |
| PBKDF2-HMAC-SHA-256 | `crux_access_port_derive` | 8 | Wrong password, `BAD_STATE`, `NOT_SUPPORTED`. Includes a 128-byte password and an empty one. |
| AES-256-GCM | `crux_credential_port_seal`, `_open` | 6 | Tampered tag, ciphertext, additional data or nonce, and a wrong key, are each `INVALID_SIGNATURE`. Also `NOT_PERMITTED` and a wrong key size. Includes a whole 16,336-byte slot payload. |
| P-256 key generation | `crux_credential_port_generate_key` | 4 | `NOT_PERMITTED` on a private export without the usage. Forms are 32 and 65 bytes. |
| Ed25519 key generation | `crux_credential_port_generate_key` | 3 | RFC 8032 known answer. Forms are 32 and 32 bytes. A changed signature is `INVALID_SIGNATURE`. |

### What a run can report

| Result | Meaning | Counts as |
| --- | --- | --- |
| `PASS` | As documented | Pass |
| `UNSUPPORTED` | The provider lacks it, and [`provider_declarations.cmake`](../tests/psa_crypto/provider_declarations.cmake) says so | Not a pass. Zephyr shows it as skipped. |
| `DEVIATION` | The case fails at its one marked assertion, with the one status named there, and the declarations say so, with the reason below. Any other status, or a failure elsewhere in the case, is `FAIL`. | Not a pass. Zephyr shows it as skipped, and the script checks exactly which cases were skipped. |
| `FAIL` | Any other failure, an undeclared gap, or a declaration that no longer holds | Failure |

### Controls

Each runs on every invocation of `scripts/test-psa-crypto.sh host`.

| Control | Result required |
| --- | --- |
| No provider selected | Configure fails, naming `CRUX_PSA_PROVIDER` |
| Stub that initialises and returns `NOT_SUPPORTED` for everything else | Every case fails except `init_is_idempotent` and the two `*_unsupported_algorithm_not_supported` cases, which expect that status. It used to fail at initialisation, so it reached nothing, which the review of `00db0bf` found. |
| Stub that returns success with zeroed output | Every case fails except `init_is_idempotent` and `ecdsa_p256_verify_hash_known_answer`, whose expected status is success and whose output is not checked |
| Each provider with no declarations | Exactly its declared cases fail, by name |
| A passing case declared unsupported | Reported as a stale declaration |
| The permissive stub, with the all-zero signature case declared a deviation | `FAIL`, because it accepted the signature and a deviation excuses only the named status. This control failed before the classifier was narrowed: [log](evidence/psa-crypto/v4.4.2-revised/red-hidden-deviation.txt). |

## Results

| Environment | TF-PSA-Crypto | wolfPSA |
| --- | --- | --- |
| Linux Host, Debug | 42 pass, 3 declared unsupported, 0 fail | 43 pass, 2 declared deviations, 0 fail |
| Linux Host, ASan and UBSan | The same, with UBSan set to halt | The same, with 4 UBSan reports inside wolfSSL recorded, below |
| `native_sim/native/64`, Twister | 43 pass and 3 skipped, including the list check | 44 pass and 2 skipped, including the list check |
| `native_sim`, with the wolfSSL TLS layer in the image | Not applicable | 44 pass and 2 skipped. The cases do not use TLS, and there is no TLS socket backend. |
| RT1060-EVKB `mimxrt1060_evk@B//qspi` | Compile only, in CI and locally, [results below](#rt1060-evkb-entropy-source) | Compile only, locally, [results below](#rt1060-evkb-entropy-source) |

Retained logs: [`docs/evidence/psa-crypto/v4.4.2-revised/`](evidence/psa-crypto/v4.4.2-revised/), from a local run with these 45 cases, revision `a0fa506`. The earlier [`v4.4.2/`](evidence/psa-crypto/v4.4.2/) logs, including CI run 41891057428, are of the 41-case form, in which a deviation stopped its case early: they stay unchanged as a record of that state. Host, `native_sim`, cross-build, CI and physical evidence are separate, and physical evidence does not exist.

### What CI keeps running

| Part | `psa-crypto` workflow | Local, `scripts/test-psa-crypto.sh all` in the Dev Container with the group enabled |
| --- | --- | --- |
| Controls, TF-PSA-Crypto on Host and `native_sim`, pins, entropy declaration | Every run | Yes |
| TF-PSA-Crypto compiled for RT1060-EVKB | Every run | With `target` |
| **wolfPSA on Host and `native_sim`** | **No** | Yes |
| wolfPSA compiled for RT1060-EVKB | **No** | With `target` |

`scripts/check-ssh-boundaries.py` and its test forbid a default workflow from selecting the secure management stacks, so a change to a wolf pin or to these cases gets no automatic wolfPSA result. That rule is unchanged: see [decision 7 of the design](psa-crypto-design.md#decisions-for-review).

Run 41891057428 is the exception. The first form of the workflow enabled the group, and that run passed all three parts with both providers while `Ssh.Boundaries` failed in `host-foundation` because of it. The workflow was then narrowed, so it is one result at one revision, with the 41-case form, and no later run repeats it. **So the 45-case wolfPSA results are local only.**

**Feasibility result: wolfPSA runs on Linux Host and `native_sim`, and passes every case that the two consumers depend on.** Its two deviations are in ECDSA P-256 verification and public-key import. Neither consumer depends on either behaviour, and the deviations do not stop the checks beside them, which pass.

### What each consumer needs, by provider

| Need | Consumer | TF-PSA-Crypto | wolfPSA |
| --- | --- | --- | --- |
| PBKDF2-HMAC-SHA-256 | `crux_access` | Yes | Yes |
| Keyed hash, HMAC-SHA-256 | `crux_access` | Yes | Yes |
| Random | Both | Yes | Yes |
| AES-256-GCM, one-shot | Credential store | Yes | Yes |
| AES-256-GCM, multipart with a separate tag | Credential store | Yes | Yes, and on `native_sim` only with `WOLFSSL_AESGCM_STREAM`, which the module's Kconfig does not offer |
| P-256 key generation and export | Credential store | Yes | Yes |
| **Ed25519 key generation and export** | Credential store | **No.** Generation is `NOT_SUPPORTED`, import is `INVALID_ARGUMENT`, and Zephyr's Kconfig has no symbol for it. A store provider's port reports this as `ERR_ENTROPY`, which is how such a request surfaces. | Yes, with `CONFIG_WOLFSSL_CURVE25519` on `native_sim` |
| `psa_key_derivation_verify_bytes` | Nobody: the Access core compares | No. An unconditional `NOT_SUPPORTED` stub. | Yes |

This answers the open point in the [gate decisions](plans/secure-management-gate-decisions.md) on whether wolfPSA can generate Ed25519: it can, on Host and `native_sim`.
The missing Ed25519 is not what ties SSH to the Wolf profile: the [SSH design](plans/secure-management-cli-sftp-design.md) allows a P-256 host key where Ed25519 is unavailable. SSH needs the Wolf profile because [the Mbed profile has no SSH](plans/secure-management-design.md#provider-profiles).

### Provider deviations

| Provider | Case | Observed | Documented | Effect |
| --- | --- | --- | --- | --- |
| wolfPSA | `ecdsa_p256_zero_signature_invalid` | An all-zero signature gives `PSA_ERROR_GENERIC_ERROR` (-132) | `PSA_ERROR_INVALID_SIGNATURE` | Still rejected. A caller must treat every status but success as rejection. Success at this assertion would be `FAIL`. |
| wolfPSA | `ecdsa_p256_off_curve_import_rejected` | `psa_import_key` accepts an off-curve P-256 point | `PSA_ERROR_INVALID_ARGUMENT` | Refused at use: `ecdsa_p256_off_curve_key_never_verifies` passes. `WOLFSSL_VALIDATE_ECC_IMPORT` did not change the import result. Do not treat import as validation of an untrusted key. |
| TF-PSA-Crypto | `ed25519_known_answer_rfc8032` | Import of a valid Ed25519 seed gives `INVALID_ARGUMENT`, where generation gives `NOT_SUPPORTED` | | The case counts this as a gap. An undeclared gap still fails, so wolfPSA cannot hide behind it. |

Neither wolfPSA deviation stops a consumer named in this issue: neither `crux_access` nor the store imports an untrusted public key through PSA. Each can be reported
upstream, and neither was.

The review of `00db0bf` found that these two used to be declared on whole cases. A declared case stopped at the deviating assertion, so the truncated-signature and missing-prefix checks after it had never run on wolfPSA, and a provider that accepted a truncated signature would have been reported as a deviation. Each case is now split so that a deviation covers one assertion, and those two checks now run and pass on wolfPSA. What each assertion expects did not change, for either provider.

### Other findings

| # | Finding | Status |
| --- | --- | --- |
| 1 | UBSan reports 4 issues inside wolfSSL under wolfPSA's standalone Host settings: signed overflow in `sp_256_num_bits_29_9` in `sp_c32.c`, and misaligned loads and stores in `ByteReverseWords64` in `misc.c` and the 64-bit `GMULT` in `aes.c`. The cases still pass, and the script pins the count, so a new one fails. Whether the Zephyr builds reach them: `sp_c32.c` and `aes.c` are compiled on `native_sim` and RT1060, and `misc.c` is included. Preprocessed with their own flags, neither `sp_256_num_bits_29_9` nor that `GMULT` is present on either, so the Zephyr settings select other variants. `ByteReverseWords64` is defined there with no caller in `aes.c`. UBSan itself ran only on Host, so this shows only that those functions are absent. | Recorded, not fixed. Vendor code. [Reports](evidence/psa-crypto/v4.4.2-revised/host-wolfpsa-ubsan-reports.txt), [source check](evidence/psa-crypto/v4.4.2-revised/zephyr-ubsan-sources.txt). |
| 2 | `native_sim` random repeats on every run, for both providers. | [Declared test-only](#provider-declarations) |
| 3 | Zephyr's glue for TF-PSA-Crypto can fall back to `sys_rand_get()` when the entropy read fails (`allow_non_cs` in `zephyr_entropy.c`). wolfCrypt's seed returns `RNG_FAILURE_E` instead. On RT1060 `sys_rand_get()` is a non-cryptographic generator. | Source read. Weighs for the Wolf profile. Whether a TRNG read can fail in practice is untested. |
| 4 | `CSPRNG_ENABLED` depends only on `ENTROPY_HAS_DRIVER`, so it is `y` with the test generator and certifies nothing. | Source and configuration read |
| 5 | Zephyr's module has no `MBEDTLS_THREADING_C` option at this pin, so TF-PSA-Crypto's core has no lock, and on the Mbed profile TLS threads call the same core. wolfPSA defaults `CONFIG_WOLFPSA_THREAD_SAFE=y`. | Source and configuration read. Concurrency is untested for both. Open for #242. |
| 6 | `tf-psa-crypto` is a commit in Zephyr's fork, whose relation to the upstream `v1.1.0` tag was not established. | Open, unchanged |
| 7 | The archive hash is a GitHub property that was not examined for stability. GoogleTest has the same exposure. If it changes, a Host configure fails loudly. | Accepted |

## Provider declarations

API conformance does not establish these four, so each is declared apart, for each provider and environment, with its evidence level.
**Run** means executed in this qualification. "Not evaluated" is not a pass.

### Algorithm support

| Environment | TF-PSA-Crypto | wolfPSA |
| --- | --- | --- |
| Linux Host | The subset without Ed25519. Run. Everything else in its default configuration is not qualified. | The subset with Ed25519. Run. Uses wolfPSA's standalone settings, unedited. |
| `native_sim` | The subset without Ed25519, built from only the `PSA_WANT` symbols in [`prj.conf`](../tests/psa_crypto/zephyr/prj.conf). Run. | The subset with Ed25519, on the wolfSSL module's Kconfig settings, the route a management image must use. Run, with and without the TLS layer. |
| RT1060-EVKB | The same configuration compiled. Compile only. | The same configuration compiled. Compile only. |

### Key storage

| Environment | TF-PSA-Crypto | wolfPSA |
| --- | --- | --- |
| Linux Host | Volatile keys, checked by `ecdsa_p256_import_is_volatile`. Persistent storage not examined. | Volatile keys, checked the same way. The standalone build has a POSIX store, not used or examined. |
| `native_sim` | Volatile. 16 static key slots (`MBEDTLS_PSA_STATIC_KEY_SLOTS`). `MBEDTLS_PSA_CRYPTO_STORAGE_C` off. Run. | Volatile, in wolfPSA's in-memory list. Zephyr secure storage is off, and no case uses a persistent key. Run. |
| RT1060-EVKB | The same. Compile only. | The same. Compile only. |

Crux does not use PSA persistent keys. The credential store owns persistence, and this issue makes no secure-storage, provisioning or key-confidentiality claim.

### Entropy source

| Environment | TF-PSA-Crypto | wolfPSA |
| --- | --- | --- |
| Linux Host | The kernel through `mbedtls_platform_get_entropy`. Two processes differ. Run. Quality is the host's, and not assessed. | The kernel through `wc_GenerateSeed`, seeding a Hash-DRBG. Two processes differ. Run. Quality not assessed. |
| `native_sim` | **Test-only.** `fake_entropy_native_sim`, through `MBEDTLS_PSA_CRYPTO_EXTERNAL_RNG`. Repeats on every run. Run. | **Test-only.** The same driver seeding the Hash-DRBG. Repeats on every run. Run. |
| RT1060-EVKB | The TRNG, [below](#rt1060-evkb-entropy-source). Sources and compile only. | The TRNG, [below](#rt1060-evkb-entropy-source). Sources and compile only. |

The reasoning and its consequence for later compositions are in [What "real" means](psa-crypto-design.md#what-real-means-on-native_sim).

### Hardware acceleration

**Unsupported, for each provider and environment.** Nothing is evidenced, and no Crux claim depends on it.

| Environment | Source reading |
| --- | --- |
| Linux Host | TF-PSA-Crypto's default configuration defines `MBEDTLS_AESNI_C`, which can use CPU instructions at run time. That is a provider detail. It was not measured, and Crux does not rely on it. wolfPSA's standalone settings do not ask for AES-NI. |
| `native_sim` | None configured |
| RT1060-EVKB | Zephyr has a separate DCP crypto driver, and neither provider's configuration uses it. `scripts/test-psa-crypto.sh target` fails if `CONFIG_CRYPTO_MCUX_DCP` is set. |

## RT1060-EVKB entropy source

**Evidence status: from sources and compile only. Not executed on a target, and target execution needs separate approval.**

| Point | Finding | Evidence |
| --- | --- | --- |
| Source | The i.MX RT TRNG: `zephyr,entropy = &trng`, `trng: random@400cc000`, compatible `nxp,kinetis-trng`, in `nxp_rt10xx.dtsi`, with no override in the board files | Source, and the check on the generated `zephyr.dts` |
| Driver | `entropy_mcux_trng.c`, `CONFIG_ENTROPY_MCUX_TRNG=y`, on the NXP SDK `fsl_trng.c` in `hal_nxp` | Source, and the check on the generated `.config` |
| Generator above it, TF-PSA-Crypto | The driver selects `ENTROPY_NEEDS_PRNG`, so the PSA source is the legacy RNG with an HMAC_DRBG over it | Source, and `.config` retained for the compile |
| Generator above it, wolfPSA | wolfCrypt's Hash-DRBG, seeded straight from the device. A failed read is `RNG_FAILURE_E`. | Source |
| Not assessed | Health-check limits, entropy rate, start-up time, SP 800-90B or AIS 31 | Nothing ran |

Compile results, from [a local compile at `a0fa506`](evidence/psa-crypto/v4.4.2-revised/local-target.txt). Each image holds the conformance cases and Ztest, so these figures are not a product's footprint. RAM includes configured stacks.

| Provider | Flash | RAM | Resolved |
| --- | --- | --- | --- |
| TF-PSA-Crypto | 120,672 B | 84,480 B | `ENTROPY_MCUX_TRNG`, `ENTROPY_NEEDS_PRNG`, `MBEDTLS_PSA_CRYPTO_LEGACY_RNG`, `MBEDTLS_PSA_DRIVER_GET_ENTROPY`, `XOSHIRO_RANDOM_GENERATOR` |
| wolfPSA, crypto only | 251 KB | 72,960 B | `ENTROPY_MCUX_TRNG`, `ENTROPY_NEEDS_PRNG`, `WOLFPSA`, `WOLFSSL_AES_TOUCH_LINES`, `XOSHIRO_RANDOM_GENERATOR` |

The script checks the entropy driver, the devicetree node, and the absence of DCP and of `CONFIG_TEST_RANDOM_GENERATOR` on each build. CI keeps doing that for TF-PSA-Crypto.

## Decisions

The manifest and module arrangement and the wolf pins come from [#198](https://github.com/wonny-labs/crux-pilot/pull/198) and [#202](https://github.com/wonny-labs/crux-pilot/pull/202). The rest is adopted on a reading of the [owner's decision](https://github.com/wonny-labs/crux-pilot/issues/183#issuecomment-6637976084), [as above](#psa-crypto-qualification-for-zephyr-and-linux-host), for the owner to confirm at merge. Reasons not to take the alternatives are in the table.

### Manifest and module

**Adopted: keep `mbedtls`, `tf-psa-crypto`, `wolfssl` and `wolfpsa` as projects of the `crux-secure-management` group, off by default.** `west.yml` does not change. #202 already
placed them, so the record's earlier proposal, `name-allowlist`, is replaced. A workspace enables them with `west config manifest.group-filter -- +crux-secure-management`, and a composition no longer
passes `ZEPHYR_MODULES`.

| Option | Result |
| --- | --- |
| **Group, as on `main`** | **Chosen.** An ordinary workspace fetches no cryptography, and one pin per project. |
| `name-allowlist` | Rejected. Every workspace would fetch them, and it would repeat what the group already holds. |
| `ZEPHYR_MODULES` from a cache | Rejected. Not reproducible from `west update`. |
| `tf-psa-crypto` alone | Not viable. The `mbedtls` module holds the CMake entry and the PSA Kconfig tree, and wolfPSA also needs them for configuration. |

### Linux Host dependency

**Adopted: [`cmake/CruxPsaCrypto.cmake`](../cmake/CruxPsaCrypto.cmake) fetches TF-PSA-Crypto as a pinned archive with a SHA-256, as GoogleTest does.** The script checks it against `west.yml`.
The root build does not call it yet, and Host CI has no west workspace. wolfPSA on Host is a test built from the west workspace.

| Option | Result |
| --- | --- |
| **`FetchContent`, URL and SHA-256 at Zephyr's pin** | **Chosen.** Fits the online `bootstrap-host.sh` and offline `check-host.sh` flow. |
| System package | Rejected. Ubuntu 24.04 offers Mbed TLS 2.28, which is neither provider. |
| West module checkout for every Host build | Rejected for the default. Host CI would need a west workspace. It stays the route for wolfPSA on Host. |
| Vendoring | Rejected: size, licence bookkeeping and maintenance. |

### API subset

**Adopted:** the [subset in the policy](native-interface-policy.md#qualified-psa-crypto-subset). `psa_key_derivation_verify_bytes` is out, because the Access core compares and TF-PSA-Crypto lacks it, which closes the record's earlier open point. The subset is for Crux's own code. TLS and SSH are not in it. On the Wolf profile they call wolfCrypt directly. On the Mbed profile Mbed TLS calls PSA itself, beyond this subset, in the same provider, and that use is **not qualified here**.

### Licence basis

Recorded in [upstream reuse](upstream-reuse.md#psa-crypto-providers), from each project's own files. This is an engineering summary, and it is not legal advice.

| Component | Licence stated by its source | Basis here |
| --- | --- | --- |
| TF-PSA-Crypto, `mbedtls` module | Apache-2.0 OR GPL-2.0-or-later | The Apache-2.0 option, matching Crux |
| wolfPSA | GPL-3.0-or-later | Fetched by west only in a workspace that enables the group, and never stored here. Test builds only, and no binary is published. |
| wolfSSL | GPL-3.0-or-later in its file headers, with `LICENSING` adding an exception for other named software | The same |

Whoever distributes an image that links a provider needs a licence basis that covers it, and establishes that for their own case. This repository states no position on anyone's licences.

## History

The first trial, at Zephyr v4.4.0 and `f57ff22`, covered 22 cases on TF-PSA-Crypto. Its logs stay unchanged in [`docs/evidence/psa-crypto/`](evidence/psa-crypto/) and its plan in
[`docs/superpowers/plans/`](superpowers/plans/2026-11-02-psa-crypto-qualification.md). Its first run found the `verify_bytes` stub and two assertions that went beyond the documented API.

## Acceptance criteria

| Criterion | Status |
| --- | --- |
| Manifest, module and Host dependency decisions, including licence, approved before adoption | Manifest and modules: met by #198 and #202. Host dependency: met on a reading of the owner's decision of 4 November 2026, for the owner to confirm. |
| Declared subset and limits documented in the native-free consumer policy | Met |
| Cases pass with the primary provider on Linux Host and `native_sim` | Met for TF-PSA-Crypto, with Ed25519 declared unsupported |
| wolfPSA feasibility result recorded, pass or not | Met: it runs, with two declared deviations, each on one assertion |
| RT1060-EVKB entropy source identified with its evidence status | Met, from sources and compile only |
| Hardware acceleration declared unsupported unless evidenced | Met |

Not run: any physical-board operation, `psa-arch-tests`, TLS through either provider, secure storage, a consumer calling a real provider, and concurrency.
