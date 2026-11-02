# PSA Crypto qualification for Zephyr and Linux Host

**Status: trial results and proposals for owner decision. Nothing here is adopted.**
This record supports [Issue #142](https://github.com/wonny-labs/crux-pilot/issues/142). The
owner's approval to start the work is recorded in
[the issue comment](https://github.com/wonny-labs/crux-pilot/issues/142#issuecomment-6565912192).
That approval covers investigation, test-first development, trial builds and a Draft PR. It does
not adopt a dependency, a manifest change, an API subset or a licence statement.

Labels used below: **Finding** (observed in source or by a run), **Proposal** (needs an owner
decision), **Implemented** (test-only code in this change). The [native-free consumer
policy](native-interface-policy.md) is unchanged by this change.

## What this change adds and what it does not

Adds only test material, a trial script and documentation:

- `tests/psa_crypto/cases/`: provider-neutral conformance cases (C17, `<psa/crypto.h>` and ISO C
  only).
- `tests/psa_crypto/host/`: a standalone Linux Host CMake project that is not part of the root
  build.
- `tests/psa_crypto/zephyr/`: a `native_sim` Ztest application that runs the same case list.
- `tests/psa_crypto/stubs/`: two test-only stub providers used as negative controls.
- `scripts/test-psa-crypto.sh`: the trial runner. It is not in any CI workflow.
- This document, the [plan](superpowers/plans/2026-11-02-psa-crypto-qualification.md) and the
  [retained evidence](evidence/psa-crypto/).

Unchanged: `west.yml`, the root `CMakeLists.txt`, `.devcontainer/`, the CI workflows and the policy
document. The provider sources are never stored in this repository.

## Existing state (at `f57ff22`)

- **Finding:** The repository contains no cryptography. `west.yml` imports Zephyr
  `684c9e8f32e4373a21098559f748f06915f950c9` (`git describe`: `v4.4.0`) with
  `name-allowlist` `cmsis`, `cmsis_6`, `hal_nxp`, `littlefs`, `fatfs`.
- **Finding:** Host CI ([`host.yml`](../.github/workflows/host.yml)) has no west workspace. It
  fetches GoogleTest with `FetchContent` (URL and SHA-256) into `CRUX_CACHE_DIR`, then
  [`scripts/check-host.sh`](../scripts/check-host.sh) runs with `FETCHCONTENT_FULLY_DISCONNECTED=ON`.
- **Finding:** `native_sim` tests pass external modules through Twister
  `--extra-args="ZEPHYR_MODULES=..."` (for example `scripts/test-storage-simulators.sh`). The trial
  uses the same mechanism.
- **Finding:** `origin/main` has no `LICENSE`, `NOTICE` or `COPYING` file, so Crux's own licence is
  not stated in the repository.

## Trial inputs

| Item | Value |
| --- | --- |
| Crux base | `f57ff22478dae1069fb220659a438e800921528c` (`origin/main`, 1 Nov 2026), branch `feat/issue-142-psa-crypto-qualification` |
| Zephyr | `684c9e8f32e4373a21098559f748f06915f950c9`, `v4.4.0` (verified with `git describe`) |
| `tf-psa-crypto` | `dc575a2ddcc8cb16275d24c42a52eaf79ebe2231`, fetched from `https://github.com/zephyrproject-rtos/tf-psa-crypto.git` with `git fetch --depth 1` (matches the Zephyr manifest) |
| `mbedtls` module | `a3e190fe44c78d1ba67f55979e1257328cc7d0d8`, from `https://github.com/zephyrproject-rtos/mbedtls.git` (matches the Zephyr manifest) |
| `psa-arch-tests` | `6e8219237435112df33bbcf37b7f5657fcfb9cff`, inspected only, not built |
| Trial directory | `/opt/crux-cache/psa-trial/` in a container of image `crux-pilot-devcontainer:local` (linux/amd64, Clang 18.1.3, CMake 3.28.4, Ninja 1.11.1, west 1.4.0). The #141 directory `/opt/crux-cache/trial/` was not touched. |
| Disk | Host volume 13 GiB free before and after (94% used). Cache volume 190 GiB free. Trial trees: `tf-psa-crypto` 26 MB, `mbedtls` 11 MB, `psa-arch-tests` 14 MB (shallow, working tree). |
| Container change | `libclang-rt-18-dev=1:18.1.3-1ubuntu1` was installed into the throwaway trial container only. See [Environment notes](#environment-notes). |

The provider pin is a commit in Zephyr's fork. Its `zephyr/module.yml` labels it TF-PSA-Crypto
`1.1.0`, but the upstream tag `v1.1.0` resolves to a different commit
(`29160dd877d29658279fd683b2ae57b320ddcf09`). **Finding:** the relation between the pin and the
upstream tag was not established here.

Commands (all inside the container, from the repository root):

```sh
scripts/test-psa-crypto.sh all      # Host controls, Host provider with and without ASan/UBSan, Twister native_sim
CLANG_FORMAT=clang-format scripts/check-format.sh
```

## Conformance cases

**Implemented.** 22 cases, listed in `PSA_CASE_LIST` in
[`psa_cases.h`](../tests/psa_crypto/cases/psa_cases.h). The cases assert behaviour that the pinned
`psa/crypto.h` documents. Behaviour it leaves undefined is not asserted: calling a function
before `psa_crypto_init()` is explicitly undefined, and `hash_length` is documented only on
success.

| Area | Cases (positive, then negative status) |
| --- | --- |
| Initialisation, random | init is idempotent; requested bytes only are written and two requests differ; a 2500-byte request succeeds |
| SHA-256 | known answers (one-shot, multipart); update after finish is `BAD_STATE`; 31-byte buffer is `BUFFER_TOO_SMALL`; mismatching or wrong-length expected hash is `INVALID_SIGNATURE` (or `INVALID_ARGUMENT` for the length); unassigned hash is `NOT_SUPPORTED` |
| ECDSA P-256 verify | volatile import; known answer over the hash; flipped, zero and truncated signatures rejected; tampered data or hash is `INVALID_SIGNATURE`; off-curve or unprefixed key is `INVALID_ARGUMENT`; no usage flag or a wildcard algorithm is `NOT_PERMITTED` |
| PBKDF2-HMAC-SHA-256 | RFC 7914 and 4096-iteration known answers; wrong password derives different output; verify-bytes when supported; output on an inactive operation is `BAD_STATE`; unassigned hash is `NOT_SUPPORTED` |
| Key identifiers | a destroyed key is `INVALID_HANDLE` for use and for a second destroy |

All known-answer values were recomputed with Python `hashlib` and OpenSSL 3.0.13 before use. See
[`vectors.h`](../tests/psa_crypto/cases/vectors.h).

### Failing before the provider is wired in

Retained in [red-no-provider-and-stubs.txt](evidence/psa-crypto/red-no-provider-and-stubs.txt)
(first form of the cases) and re-checked by the script on every run:

| Control | Result |
| --- | --- |
| No provider selected | CMake configure fails with a message naming `CRUX_PSA_PROVIDER` |
| Stub that returns `PSA_ERROR_NOT_SUPPORTED` | every case fails |
| Stub that returns `PSA_SUCCESS` | every case that expects an error status fails; only `init_is_idempotent` and `ecdsa_p256_verify_hash_known_answer` pass, and the script rejects any other pass |

The permissive stub exposed one weak case during development (a "differs from the expected
output" check that zeros satisfy). It was strengthened to require the correct output first.

### Results with TF-PSA-Crypto

| Environment | Result | Evidence |
| --- | --- | --- |
| Linux Host, Debug, Clang 18 | 22 of 22 pass (one with a note, below) | [first run](evidence/psa-crypto/host-tf-psa-crypto-first-run.txt) and the script run recorded in the PR |
| Linux Host, ASan and UBSan | 22 of 22 pass | script run recorded in the PR |
| Zephyr `native_sim/native/64`, Twister | 22 cases plus one list-completeness check pass | script run recorded in the PR |

**Finding (first Host run).** Three of 23 cases failed against the real provider. Checking the
pinned header and the provider source showed two assertions that went beyond the documented API
(the `hash_length` value after `BUFFER_TOO_SMALL`; a single code for a non-hash algorithm, where
the header documents both `NOT_SUPPORTED` and `INVALID_ARGUMENT`) and one provider limitation:

- **Finding:** `psa_key_derivation_verify_bytes()` is an unconditional stub that returns
  `PSA_ERROR_NOT_SUPPORTED` in this provider revision (`core/psa_crypto.c`, definition at line
  6735 of the pinned tree). The case `pbkdf2_verify_bytes_when_supported` reports this as a note
  next to PASS rather than passing silently.

Cases passed here mean the same cases pass with this provider in these two environments. They are
not a claim of general PSA Crypto API conformance.

## Proposed API subset

**Proposal, not decided.** Derived from the four planned consumers. Every function has a stated
consumer. The cases call these functions and no others, apart from the test-only ones marked.

| Consumer | Crux code calls PSA? | Functions | Algorithms and key material |
| --- | --- | --- | --- |
| Random values for keys and nonces | Yes | `psa_crypto_init`, `psa_generate_random` | none |
| Application image check (ECDSA P-256 signature and SHA-256 before a Flash write) | Yes | `psa_hash_setup`, `psa_hash_update`, `psa_hash_finish`, `psa_hash_verify`, `psa_hash_abort`, `psa_hash_compute` (small metadata), `psa_import_key`, `psa_destroy_key`, `psa_verify_hash`, key-attribute setters (`psa_set_key_type`, `_bits`, `_usage_flags`, `_algorithm`), `psa_reset_key_attributes` | SHA-256; ECDSA with SHA-256 verification only; P-256 public key as a volatile, uncompressed point with `VERIFY_HASH` usage. A multipart hash then `psa_verify_hash` suits an image that does not fit in RAM; `psa_verify_message` is therefore not proposed. |
| Credential verification (local login) | Yes | `psa_key_derivation_setup`, `psa_key_derivation_input_integer` (cost), `psa_key_derivation_input_bytes` (salt, password), `psa_key_derivation_output_bytes`, `psa_key_derivation_abort` | PBKDF2-HMAC-SHA-256 with a caller-chosen iteration count. `psa_key_derivation_verify_bytes` is wanted for a constant-time comparison but is **unsupported by this provider revision**; see decision 3. |
| TLS client and server | No. The TLS library, the Zephyr default provider, calls PSA internally. | none by Crux code | Named for later, **not qualified here** (TLS is out of scope): ECDHE and ECDSA on P-256, an AEAD, HMAC, HKDF and SHA-256 or SHA-384. Qualify them when a TLS consumer is planned. |

Test-only use, not a consumer interface: `psa_get_key_attributes` with the attribute getters
(proves the key lifetime is volatile).

Limits that go with the subset: volatile keys only; no persistent keys, secure storage,
provisioning or key-confidentiality claim; single-threaded use was tested, concurrency was not;
interrupt-context use was not examined; stack, heap and timing were not measured on a target.

## Provider declarations

**TF-PSA-Crypto `dc575a2d`.** API conformance does not establish these four items, so each is
declared separately with its evidence status. "Not evaluated" is not a pass.

| Item | Linux Host | Zephyr `native_sim` | RT1060-EVKB (`mimxrt1060_evk@B//qspi`) |
| --- | --- | --- | --- |
| Algorithm support | The subset above passes the cases. Everything outside the subset is not qualified. `verify_bytes` is unsupported. *Run on Host.* | Same, built with only the `PSA_WANT_*` symbols the subset needs ([`prj.conf`](../tests/psa_crypto/zephyr/prj.conf)). *Run on `native_sim`.* | Not built or executed. Kconfig resolves with the same symbols (configure only). |
| Key storage | Volatile lifetime is checked by `ecdsa_p256_import_is_volatile`. Persistent storage and slot configuration were not examined. | Volatile lifetime checked. Static key slots, 16 (`MBEDTLS_PSA_STATIC_KEY_SLOTS=y`). `MBEDTLS_PSA_CRYPTO_STORAGE_C` not enabled. | Static slots, 16, in the resolved Kconfig. Not built. |
| Entropy source | Linux `getrandom()` through `mbedtls_platform_get_entropy` (source read, `platform/platform_util.c`). The cases exercise `psa_generate_random` and it works. Entropy quality not assessed. | **Test-only and deterministic.** The simulator's generator repeats its output on every run unless seeded differently ([probe](evidence/psa-crypto/native-sim-entropy-probe.txt)). It must not be used for any security claim. | The NXP TRNG through `entropy_mcux_trng`, see below. *Source read and Kconfig resolved, not executed.* |
| Hardware acceleration | **Unsupported** (none configured or evidenced for the subset; CPU-instruction options in the provider were not evaluated). | **Unsupported.** | **Unsupported.** The Zephyr tree has a separate DCP crypto driver (`crypto_mcux_dcp.c`, node enabled in `nxp_rt10xx.dtsi`), but no PSA accelerator integration exists in `modules/mbedtls` at the pin, and it is not enabled in the resolved configuration. |

## RT1060-EVKB entropy source

**Evidence status: source-read and Kconfig-resolved. Not built, not executed on a target.** Target
execution needs a separate approval.

- **Finding:** The chosen entropy device is the i.MX RT TRNG. `nxp_rt10xx.dtsi` sets
  `zephyr,entropy = &trng` and defines `trng: random@400cc000` with compatible
  `nxp,kinetis-trng`, interrupt 53, `status = "okay"`. The board files in
  `boards/nxp/mimxrt1060_evk/` contain no override.
- **Finding:** The driver is `drivers/entropy/entropy_mcux_trng.c` (Kconfig `ENTROPY_MCUX_TRNG`,
  default y) on the NXP SDK `fsl_trng.c` in `hal_nxp`. The HAL default configuration uses ring
  oscillator clocking with raw sampling and sets statistical health-check limits (monobit, run
  lengths). Their adequacy and the entropy rate were not assessed. No SP 800-90B or AIS 31
  assessment exists in this repository.
- **Finding:** The driver's Kconfig selects `ENTROPY_NEEDS_PRNG`, and its comment says the TRNG is
  not designed to supply a continuous stream and should seed a generator. At the pin this makes
  `MBEDTLS_PSA_CRYPTO_LEGACY_RNG` the PSA random source, with an HMAC_DRBG over the entropy
  driver, and `XOSHIRO_RANDOM_GENERATOR` the source for `sys_rand_get()`
  ([resolved Kconfig, configure only](evidence/psa-crypto/rt1060-kconfig-resolution-configure-only.txt)).
- **Finding (risk):** In `modules/mbedtls/zephyr_entropy.c`, `mbedtls_platform_get_entropy()`
  calls the entropy device, and if that call returns an error it falls back to `sys_rand_get()`
  and still reports `8 * output_size` bits of entropy. (A device that is not ready returns an
  error before that point and does not fall back.) On this board `sys_rand_get()` is the xoshiro
  generator, which Zephyr's own help text calls non-cryptographically secure. A failing TRNG read
  would therefore not stop key generation. Whether the TRNG read can fail in practice was not
  tested.
- **Finding:** `CSPRNG_ENABLED` depends only on `ENTROPY_HAS_DRIVER`. It is `y` on `native_sim`
  with the test-only generator, so it does not certify an entropy source.

## wolfPSA and other alternative providers

**Blocked on licence review. Not fetched, not built, not run.** The feasibility result for #142 is
therefore not recorded. The review must answer:

1. Under which licence terms would Crux obtain and use the source (an open-source copyleft
   licence or a commercial one), and is either acceptable for a public repository and for firmware
   that is distributed?
2. Whether the trial fetch itself is allowed for evaluation under those terms.
3. Compatibility with Crux's own licence, which is not yet stated, and with Zephyr's Apache-2.0 in
   one firmware image.
4. The obligations that linking into firmware triggers (source offer, relinking, notices) and who
   carries them.
5. Which exact repository and release is meant, which PSA Crypto API version it implements, and
   whether any patent or export-control conditions apply.
6. A statement that no commercially licensed or employer-owned code, document or licence key is
   used, so that any such material held by a person involved stays out.
7. Who signs off the statement.

## `psa-arch-tests`

Not built, not run. **Finding:** the pinned tree (Apache-2.0) has `api-tests/dev_apis/crypto` with
68 test directories and platform targets `tgt_dev_apis_stdc` and `tgt_dev_apis_linux`, so the
crypto suite is not tied to TF-M in principle. Zephyr's module entry holds only a `module.yml`.

Trade-off: it covers far more than the subset, so it is useful to choose and classify cases, but it
brings its own validation layer and CMake flow, a 14 MB second checkout and its own pass/fail
oracle for little extra evidence about four consumers. **Proposal:** use it as a reference only and
do not depend on it. A one-off Host run as an independent cross-check can be a separate task.

## Decision proposals

**None of these is decided.** Each needs an owner decision before adoption.

### 1. Manifest change

| Option | Notes |
| --- | --- |
| **M1 (recommended):** add `mbedtls` and `tf-psa-crypto` to `name-allowlist` in `west.yml`, when a consumer is approved | Both are needed at this pin: `modules/mbedtls/CMakeLists.txt` adds the `mbedtls` module as the build entry and takes the provider from `ZEPHYR_TF_PSA_CRYPTO_MODULE_DIR`. The trial built with both and with no other module (`mldsa-native` was not needed). Do not add `psa-arch-tests` or `mbedtls-3.6`. |
| M2: keep the manifest and pass the modules with `ZEPHYR_MODULES` from a cache | No adopted dependency, but builds are not reproducible from `west update` and every consumer has to provision the cache. |
| M3: add `tf-psa-crypto` only | Not viable at this pin, by the CMake entry above. |

### 2. Linux Host dependency

| Option | Notes |
| --- | --- |
| **A (recommended):** `FetchContent` with a URL and SHA-256 at the same commit as Zephyr's pin, like GoogleTest | Fits the existing online `bootstrap-host.sh` then offline `check-host.sh` flow and needs no west in Host CI. The archive `https://github.com/zephyrproject-rtos/tf-psa-crypto/archive/dc575a2ddcc8cb16275d24c42a52eaf79ebe2231.tar.gz` was downloaded for the trial: 3,747,713 bytes, SHA-256 `1a122bcb91fc7c32060e64feeea29364a7cd894413e0ba89ee200fc5e384f95b`, 686 entries, generated sources included. Needs a check that the Host pin equals the Zephyr manifest pin, and the cache pre-populated for the offline step. Archive hash stability is a GitHub property that was not examined. |
| B: system package | Rejected: Ubuntu 24.04 offers `libmbedtls-dev` 2.28.8 (checked with `apt-cache policy`), which is neither TF-PSA-Crypto nor the provider Zephyr uses. |
| C: use the west module checkout | One pin, but Host CI would need a west workspace and a manifest change first (decision 1). |
| D: vendor the sources | Rejected: size, licence bookkeeping and maintenance. |

### 3. Declared API subset

| Option | Notes |
| --- | --- |
| **S1 (recommended):** the [subset above](#proposed-api-subset), from the four consumers | Small, every function has a consumer, and each is covered by cases. |
| S2: a TLS-ready subset now (AEAD, ECDH, HKDF and more) | More to qualify with no Crux consumer; TLS is out of scope here. |
| S3: defer the subset until a consumer lands | Leaves the cases unanchored. |

Within S1 an **open point needs a decision**: `psa_key_derivation_verify_bytes` is unsupported, so
the credential consumer has no provider-supported constant-time comparison. Options: (i) a
consumer-owned constant-time comparison after `psa_key_derivation_output_bytes`, which adds
security-sensitive code outside the provider; (ii) a provider revision or patch that implements
it; (iii) route the comparison through another PSA function such as `psa_hash_verify`, which
needs review and adds to the subset. The final policy text belongs in
[the native-free consumer policy](native-interface-policy.md) and is not changed here.

### 4. Licence compatibility statement

**Findings** (from file headers and licence files; not a legal opinion; headers only were scanned):

| Component | Licence found |
| --- | --- |
| TF-PSA-Crypto `dc575a2d` | `LICENSE`: Apache-2.0 OR GPL-2.0-or-later. 253 SPDX lines in C, header, Python and CMake files carry that expression. Exceptions: `drivers/everest/**` (4 files, Apache-2.0 only) and `configs/ext/crypto_config_profile_medium.h` (BSD-3-Clause). |
| `mbedtls` module `a3e190fe` | `LICENSE`: Apache-2.0 OR GPL-2.0-or-later; all 69 SPDX lines found in `library/` and `include/` carry it. |
| Zephyr glue (`zephyr_entropy.c`, `zephyr_init.c`, read for this record) | Apache-2.0 |
| `psa-arch-tests` (reference only) | Apache-2.0 `LICENSE.md`; one file with a BSD-3-Clause SPDX line |
| wolfPSA | not reviewed (blocked) |

**Proposal:** if Crux states Apache-2.0 for itself, elect the Apache-2.0 option of the dual licence
for TF-PSA-Crypto and the Mbed TLS module, record the source, revision and licence in
[upstream-reuse](upstream-reuse.md) on adoption, and do not elect GPL-2.0-or-later. This cannot be
completed until the owner states Crux's own licence (**Finding:** none is stated at `f57ff22`).

## Open findings

1. `psa_key_derivation_verify_bytes` is unsupported in the pinned provider (decision 3).
2. The Zephyr entropy glue can fall back to a non-cryptographic generator and still report full
   entropy; on RT1060 the fallback is xoshiro (RT1060 entropy section).
3. `native_sim` PSA random output is deterministic by default. Any test that needs fresh random
   data on the simulator must say so.
4. The RT1060 TRNG configuration and entropy rate were not assessed, and nothing ran on a board.
5. The provider pin is a Zephyr fork commit whose relation to the upstream `v1.1.0` tag was not
   established.
6. Crux has no stated licence, which blocks the compatibility statement.
7. Host CI has no west workspace, which constrains the Host dependency options.
8. The local `crux-pilot-devcontainer:local` image lacks `libclang-rt-18-dev`, so `host-sanitize`
   does not link in it until the package is added (next section).

## Environment notes

- The Dockerfile lists `libclang-rt-18-dev=1:18.1.3-1ubuntu1`, but the local image did not contain
  it, so ASan and UBSan could not link. The pinned package was installed into the throwaway trial
  container only. The image, the Dockerfile and `.devcontainer/` were not changed. Whether the
  image is stale or built from an older Dockerfile was not investigated.
- The trial container did not set `CLANG_FORMAT`, so `scripts/check-format.sh` ran with
  `CLANG_FORMAT=clang-format` (the venv's clang-format 18.1.8), as `devcontainer.json` does.

## Acceptance criteria status

| Criterion | Status |
| --- | --- |
| Manifest, module and Host dependency decisions, including licence, approved before adoption | **Not met.** Prepared as proposals 1, 2 and 4. Nothing adopted or fetched into the build. |
| Declared subset and limits documented in the native-free consumer policy | **Not met.** The subset is a proposal in this document. The policy is unchanged until decision 3. |
| Cases pass with the primary provider on Linux Host and `native_sim` | **Met for the trial**, with TF-PSA-Crypto from outside the repository. Not part of the adopted build or of CI. |
| wolfPSA feasibility result recorded, pass or not | **Not met.** Blocked on licence review; not run. |
| RT1060-EVKB entropy source identified with its evidence status | **Met** at *source-read, Kconfig-resolved, not executed* status. Target execution is not approved. |
| Hardware acceleration declared unsupported unless evidenced | **Met.** Unsupported for all three environments. |

Not run: GitHub Actions, any RT1060 build or cross-compilation with the PSA application (the
Kconfig was resolved with a configure-only run), any physical-board operation, `psa-arch-tests`,
wolfPSA, TLS, secure storage.
