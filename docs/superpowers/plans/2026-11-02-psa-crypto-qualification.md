# PSA Crypto qualification (Issue #142) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Provider-neutral PSA Crypto conformance cases, trial-qualified against TF-PSA-Crypto on Linux Host and Zephyr `native_sim`, with per-provider declarations and decision proposals, without adopting any dependency.

**Architecture:** The cases are plain C17 that include only `<psa/crypto.h>` and ISO C. A case returns a failure text or `NULL`. Two thin harnesses run them: a standalone Linux Host CMake project and a Zephyr Ztest application. The provider is selected by the composition (a CMake cache variable on Host, `ZEPHYR_MODULES` on Zephyr), never by the cases. Provider sources stay outside the repository in `/opt/crux-cache/psa-trial/`. Nothing is added to the root CMake build or to `west.yml`.

**Tech Stack:** C17, CMake 3.28, Ninja, Clang 18, Zephyr v4.4.0 (`684c9e8f`), Twister/Ztest, TF-PSA-Crypto `dc575a2d` (v1.1.0), Mbed TLS module `a3e190fe` (v4.1.0, carries the Zephyr CMake entry).

**Spec:** Issue #142 (`wonny-labs/crux-pilot`, read in full 2 Nov 2026) and the owner's task brief of 2 Nov 2026. Approval recorded at issue #142 comment 6565912192.

## Global Constraints

- Branch `feat/issue-142-psa-crypto-qualification` from `origin/main` `f57ff22`, in its own worktree. No push to `main`, no merge, no `.devcontainer/` change.
- `west.yml` and the root `CMakeLists.txt` stay unchanged until the owner approves the manifest and Host dependency decisions.
- Do not fetch or build wolfPSA or any other alternative provider. Record it as blocked on licence review.
- No hardware operation. The RT1060-EVKB entropy source is identified from source and documentation only.
- Out of scope: TLS, secure storage, key provisioning, key-confidentiality claims. Volatile keys only.
- Trial directory is `/opt/crux-cache/psa-trial/`. The #141 files under `/opt/crux-cache/trial/` are not touched.
- Owned Host C targets use `-Wall -Wextra -Wpedantic -Werror`; owned Zephyr test targets use `-Wall -Wextra -Werror`.
- Commits use `<type>: <summary>` without scope. Report only what was run. Keep Host, `native_sim`, cross-build, CI and physical evidence separate.

## Review Focus

- Provider returns success for everything (permissive stub): negative cases must fail. Pinned by the mutation control in Task 3.
- Provider returns `PSA_ERROR_NOT_SUPPORTED` for everything: all positive cases must fail. This is the RED run in Task 3.
- Use before `psa_crypto_init`: `PSA_ERROR_BAD_STATE` expected. Pinned in Task 2.
- Output buffer one byte too small for hash, random-free derivation and signature verification inputs: `PSA_ERROR_BUFFER_TOO_SMALL`, output untouched. Pinned in Task 2.
- Unsupported algorithm requested through an otherwise valid key: `PSA_ERROR_NOT_SUPPORTED`, not a crash. Pinned in Task 2.

---

### Task 1: Investigate and record what exists

**Files:**
- Create: `docs/psa-crypto-qualification.md` (sections: Status, Existing state, Trial inputs)

**Interfaces:**
- Produces: the recorded baseline facts the later tasks cite (pins, licences, Zephyr integration route, existing test patterns).

- [x] **Step 1:** Verify the brief's known facts against `west.yml`, Zephyr `west.yml` and the fetched trial trees.
- [x] **Step 2:** Fetch `tf-psa-crypto` and `mbedtls` at the pinned SHAs into `/opt/crux-cache/psa-trial/` and record disk space before and after.
- [ ] **Step 3:** Inspect `psa-arch-tests` at its pin in the trial directory (not fetched into the repository). Record usable parts and the trade-off.
- [ ] **Step 4:** Record the existing Host and `native_sim` test patterns (standalone consumer projects, `ZEPHYR_MODULES` through Twister `--extra-args`).

### Task 2: Provider-neutral conformance cases (red-first)

**Files:**
- Create: `tests/psa_crypto/cases/psa_cases.h`, `tests/psa_crypto/cases/psa_cases.c`, `tests/psa_crypto/cases/vectors.h`
- Create: `tests/psa_crypto/host/CMakeLists.txt`, `tests/psa_crypto/host/main.c`

**Interfaces:**
- Produces: `typedef const char *(*psa_case_fn)(void);` and `struct psa_case { const char *name; psa_case_fn run; };` with `extern const struct psa_case psa_cases[]; extern const size_t psa_case_count;`. A case returns `NULL` on success or a static string `"file:line: expression"` on failure. Cases call `psa_crypto_init()` themselves except the one that checks use before init, which is first in the table and runs in its own process.

Cases (names are the contract): `use_before_init_is_bad_state`; `init_is_idempotent`; `random_fills_buffer_and_differs`; `random_zero_length_succeeds`; `sha256_known_answer_one_shot`; `sha256_multipart_matches_one_shot`; `sha256_buffer_too_small`; `sha256_compare_mismatch_is_invalid_signature`; `hash_unsupported_algorithm_not_supported`; `ecdsa_p256_verify_known_answer`; `ecdsa_p256_verify_wrong_signature_invalid`; `ecdsa_p256_verify_tampered_message_invalid`; `ecdsa_p256_import_invalid_key_rejected`; `ecdsa_p256_verify_without_usage_flag_not_permitted`; `pbkdf2_hmac_sha256_known_answer`; `pbkdf2_verify_wrong_password_invalid`; `pbkdf2_output_over_capacity_insufficient_data`; `destroyed_key_handle_invalid`.

- [ ] **Step 1: Write the cases and the Host harness** exactly against the interface above. The harness prints `PASS name` / `FAIL name: text` per case, exits non-zero on any failure or when the case count differs from the expected constant.
- [ ] **Step 2: Run with no provider** (`cmake -S tests/psa_crypto/host -B build/psa-none`): configure must fail with a message naming `CRUX_PSA_PROVIDER`. Keep the output as evidence.

### Task 3: Stub providers prove the cases can fail

**Files:**
- Create: `tests/psa_crypto/stubs/unsupported.c`, `tests/psa_crypto/stubs/permissive.c`
- Modify: `tests/psa_crypto/host/CMakeLists.txt` (providers `stub-unsupported`, `stub-permissive`)

- [ ] **Step 1:** Implement the PSA entry points the cases call, returning `PSA_ERROR_NOT_SUPPORTED` (unsupported stub) and `PSA_SUCCESS` with zero-filled outputs (permissive stub). Headers come from the pinned module `include/` directory only; no module source is compiled.
- [ ] **Step 2: Run both.** Expected: every case fails with `unsupported`; every negative case fails with `permissive`. This is the recorded RED.

### Task 4: Qualify TF-PSA-Crypto on Linux Host

**Files:**
- Modify: `tests/psa_crypto/host/CMakeLists.txt` (provider `tf-psa-crypto` from `-DCRUX_PSA_TF_PSA_CRYPTO_DIR`)
- Create: `scripts/test-psa-crypto.sh` (Host part, runs in the Dev Container image only)

- [ ] **Step 1:** Wire the provider, configure with the trial directory, build with `-Wall -Wextra -Wpedantic -Werror` on owned sources only, run. Expected: all cases PASS.
- [ ] **Step 2:** Repeat with `-fsanitize=address,undefined`. Record the TF-PSA-Crypto configuration used (volatile keys only, no storage).

### Task 5: Qualify TF-PSA-Crypto on Zephyr `native_sim`

**Files:**
- Create: `tests/psa_crypto/zephyr/{CMakeLists.txt,prj.conf,testcase.yaml,src/main.c}`
- Modify: `scripts/test-psa-crypto.sh` (Twister part)

- [ ] **Step 1:** Ztest suite that iterates `psa_cases[]` (one test per entry, same table). Twister runs `native_sim/native/64` with `ZEPHYR_MODULES=<trial mbedtls>;<trial tf-psa-crypto>`.
- [ ] **Step 2:** Run; expected all cases PASS. Record the Kconfig symbols that were needed.

### Task 6: Provider declarations and RT1060-EVKB entropy

**Files:**
- Modify: `docs/psa-crypto-qualification.md` (sections: Provider declarations, RT1060-EVKB entropy, wolfPSA blocker)

- [ ] **Step 1:** Per provider declare algorithm support, key storage, entropy source, hardware acceleration (unsupported unless evidenced), each with its evidence status.
- [ ] **Step 2:** Identify the RT1060-EVKB entropy source from devicetree, Kconfig and driver source at the Zephyr pin. Label `source-read, not executed on target`.
- [ ] **Step 3:** Record wolfPSA as blocked on licence review and list what the review must answer.

### Task 7: Decision proposals

**Files:**
- Modify: `docs/psa-crypto-qualification.md` (sections: Proposed subset, Decision proposals)

- [ ] **Step 1:** Consumer-to-function table for TLS, credential verification, image check, random generation; keep the subset to what a consumer needs.
- [ ] **Step 2:** For the manifest, Linux Host dependency, API subset and licence statement: options, trade-offs, recommendation, marked `Proposal, not decided`.

### Task 8: Checks, Draft PR, independent review

- [ ] **Step 1:** Run `scripts/check-host.sh`, `CRUX_HOST_PRESET=host-sanitize scripts/check-host.sh`, `scripts/check-format.sh`, `scripts/analyze-host.sh`, `scripts/check-dependencies.sh`, `scripts/test-psa-crypto.sh`. Confirm portable roots include no Zephyr, board or vendor headers.
- [ ] **Step 2:** Open a Draft PR linking #142 with revision, commands and outcomes, tests not run, trial inputs, proposals, wolfPSA blocker and open findings.
- [ ] **Step 3:** Independent reviewer (not the author) returns PASS, REVISE or ESCALATE. Fix REVISE, then re-review. Record the result in the PR.
