# Dependency review: Mbed TLS 4.1.1 and TF-PSA-Crypto 1.1.1 security releases

Status: **review record, the first use of the [dependency review guide](dependency-review.md)**, 14 November 2026. Baseline: `origin/main` at `8b48e33175031a0b9d32876789b03f5eb3d4f997`. Author: Claude Code, model `claude-sonnet-5-5`. Under [ADR-0006](decisions/0006-cryptographic-dependency-policy.md) (accepted, Q2) the first pin move starts after this review, as its own PR.

This record is documentation only. It moves no pin, edits no `west.yml`, `cmake/CruxPsaCrypto.cmake` or `scripts/test-psa-crypto.sh`, and approves nothing. The owner approves the pin move itself, and the protected change to `scripts/test-psa-crypto.sh` needs its own approval (see [the plan](#plan-for-the-pin-move-evidence)).

**Labels.** **Verified** means I read it at the baseline revision and cite `path:line`, or I read it from an upstream repository with `gh api` or `curl` on 14 November 2026 and cite the URL. **Inference** is my reading. **Not verified** means no run or read exists. No build, `west` command, `native_sim` run or test ran: the Crux configuration is read from files, not from a resolved `.config` (see [what could not be established](#what-could-not-be-established)).

## Result

- **Verdict for the pin move: take, not an emergency.** Of the 18 upstream advisories of the two 7 July 2026 releases, I found **no one that the current Crux configuration reaches**, with one exception that I cannot rule out: the zero-length ECC public key read (CVE-2026-50583, LOW), reachable through the parse of an administrator-installed certificate. One more, the NIST curve reduction side channel (CVE-2026-54435, MEDIUM), does not apply to Zephyr v4.4.2's configuration and would apply under the newer Zephyr setting that turns the option on.
- **The fixes are in Zephyr's `v4.4-branch` revisions.** For every advisory that lists fix commits on the 4.1.x or 1.1.x branch, every listed commit is an ancestor of `098e120a` (`mbedtls`) and `fa923495` (`tf-psa-crypto`), and none is an ancestor of the current pins, except 1 of the 3 commits of one advisory. Ancestry shows the commit is present, not that the fix works in Crux's build.
- **Most advisories are not reached because of what the Mbed profile leaves off:** TLS 1.3 only on the listener, no session tickets, no PSK or J-PAKE, no DTLS, no peer certificate verification, no PKCS #7, no RSA, no ChaCha20.
- **That holds only as long as the configuration stays as it is.** Section 4 lists the changes that would make the "not applicable" verdicts false.
- **Not established:** a resolved `.config` of the HTTPS image, and whether the module revisions work with Zephyr v4.4.2's glue (nobody at Zephyr tested that pair).

## 1. Subject

| Item | Value | Status |
| --- | --- | --- |
| Current pins | `mbedtls` `a3e190fe44c78d1ba67f55979e1257328cc7d0d8` (Mbed TLS 4.1.0 plus 8 Zephyr commits), `tf-psa-crypto` `dc575a2ddcc8cb16275d24c42a52eaf79ebe2231` (TF-PSA-Crypto 1.1.0 plus 10) | Verified: `west.yml:28-39`, [ADR-0006 section 2.2](decisions/0006-cryptographic-dependency-policy.md#22-how-zephyrs-commits-relate-to-the-upstream-tags) |
| Candidate revisions | `mbedtls` `098e120afb51877ed814bdc04443890ae12e0642` (4.1.1 plus 8), `tf-psa-crypto` `fa92349590a54959ee2efc21b668e5afdb144726` (1.1.1 plus 24). Zephyr put them on `v4.4-branch` in commit `9b9a84fc86954527322fdfb3e05628c5b3928a64` on 13 August 2026 | Verified: [ADR-0006 section 2.3](decisions/0006-cryptographic-dependency-policy.md#23-how-zephyr-updates-these-modules-and-handles-security-fixes) |
| Upstream releases | Mbed TLS 4.1.1 and TF-PSA-Crypto 1.1.1, both 7 July 2026, on the 4.1 and 1.1 LTS branches, "supported ... until at least March 2029". No newer 4.1.x or 1.1.x release existed on 14 November 2026 | Verified: <https://github.com/Mbed-TLS/mbedtls/releases/tag/mbedtls-4.1.1>, <https://github.com/Mbed-TLS/TF-PSA-Crypto/releases/tag/tf-psa-crypto-1.1.1>, releases API |
| Advisories read | 18: 13 of the 4.1.1 release and 5 of the 1.1.1 release, each from <https://github.com/Mbed-TLS/mbedtls-docs/tree/main/security-advisories> (files `mbedtls-security-advisory-2026-07-*.md`), read on 14 November 2026 | Verified |
| Advisories not in scope | Two further July advisories (CVE-2026-50713 and CVE-2026-35336) affect Mbed TLS 3.6.x only and name Mbed TLS 4.0 and later as not affected. The 11 advisories dated March 2026 were not read: the current pins contain the 4.1.0 tag commit (`ahead_by` 8, `behind_by` 0), and 4.1.0 is dated 31 March 2026, so I **assume** they are in it | Verified for the two 3.6-only texts. The March assumption is **Not verified** |

## 2. How this was done

For each advisory: read the upstream text for its affected versions, conditions, work-around and fix commits. Read the Crux configuration for what the Mbed profile compiles and what Crux calls. Check the fix commits against both pins with `gh api repos/zephyrproject-rtos/<module>/compare/<fix>...<pin>`: a status of `ahead` or `identical` means the commit is an ancestor of the pin, `diverged` or `behind` means it is not. I tested every hash that the 4.1.x and 1.1.x columns list, and both ends of a range, but not each commit inside a range. The method is in the [guide](dependency-review.md#check-that-a-revision-holds-a-fix).

I used no ECC skill that produced a result. `ecc:security-review` is web and npm oriented and I did not use it; the checklist is the guide's step list. I did not download or run any proof-of-concept code, and the advisory texts contain no secrets.

## 3. What the Crux Mbed profile compiles and calls

### 3.1 The TLS listener

| Fact | Evidence | Status |
| --- | --- | --- |
| The Mbed profile compiles TLS 1.2 and TLS 1.3, the ephemeral TLS 1.3 key exchange and `ECDHE-ECDSA` with AES-128-GCM. It sets no PSK key exchange, no J-PAKE, no session tickets and no DTLS | `tests/native/tls_offload_probe/mbed.conf:11-16` (the file has 29 lines), `tests/zephyr/web_compile/mbedtls.conf` (the same set for RT1060) | Verified for the files. Whether these are all the symbols in effect: see section 3.4 |
| The web adapter creates the listener with `NET_IPPROTO_TLS_1_3` and one cipher suite, `TLS_AES_128_GCM_SHA256` (0x1301) | `components/web/ports/zephyr/web_http_server.c:102` and `:115-116` | Verified |
| Zephyr's TLS socket layer reads that protocol as a minimum: `mbedtls_ssl_conf_min_tls_version(..., TLS1_3)`. A TLS 1.2 client is refused with a `protocol_version` alert | Zephyr v4.4.2 `subsys/net/lib/sockets/sockets_tls.c:1799-1800`, `docs/https-composition.md:98-105` and `:127` | Verified for the source and the document. The refusal was observed on the wire, **Reported** by the document |
| The server does not verify peers: the layer keeps Mbed TLS's default authentication mode ("none for server") unless a verify level is set, and the web adapter sets none. Zephyr's layer never calls `mbedtls_ssl_conf_sig_algs`, `mbedtls_ssl_conf_groups` or `mbedtls_x509_crt_verify` | `sockets_tls.c:1860-1864` (Zephyr v4.4.2), a `grep` of that file for the three calls, and `grep` of `components/` for `PEER_VERIFY` and `TLS_PEER` (no match) | Verified |
| Zephyr parses the server's own certificate with `mbedtls_x509_crt_parse` (or the DER variant) and its key with `mbedtls_pk_parse_key` | `sockets_tls.c:1416-1421`, `:1455` | Verified |
| Zephyr calls `mbedtls_ssl_session_reset` for reused sockets | `sockets_tls.c:1578` | Verified |
| The client side of Mbed TLS is compiled whenever TLS 1.2 or 1.3 is: `MBEDTLS_SSL_SRV_C` and `MBEDTLS_SSL_CLI_C` are defined together | Zephyr v4.4.2 `modules/mbedtls/configs/config-mbedtls.h:38-44` | Verified |
| No Crux component opens a TLS client connection. The services run a server only | A `grep` of `components/` for `zsock_connect`, `getaddrinfo`, `NET_IPPROTO_TLS` and `ZSOCK_TLS_SEC_TAG_LIST` matched only the listener's `NET_IPPROTO_TLS_1_3` (`web_http_server.c:115`) and a comment, and a `grep` for `mbedtls_` and `x509_crt_verify` matched nothing | Verified for `components/`. **Not verified** for a product's own code |

### 3.2 What Zephyr's configuration header defines

| Feature | Defined only if | Crux's files set it? | Evidence at Zephyr v4.4.2 | Status |
| --- | --- | --- | --- | --- |
| TLS 1.3 session tickets (`MBEDTLS_SSL_SESSION_TICKETS`) | `CONFIG_MBEDTLS_SSL_SESSION_TICKETS`, a `bool` with no default | No | `Kconfig.mbedtls:65`, `config-mbedtls.h:55-58` | Verified by reading |
| TLS 1.3 early data (`MBEDTLS_SSL_EARLY_DATA`) | a TLS 1.3 PSK or PSK-ephemeral key exchange mode is set | No: only the ephemeral mode | `config-mbedtls.h:101-113`, `Kconfig.mbedtls:150-153` | Verified by reading |
| PSK and ECDHE-PSK key exchange, J-PAKE | `CONFIG_MBEDTLS_KEY_EXCHANGE_PSK_ENABLED`, `..._ECDHE_PSK_ENABLED`, `..._ECJPAKE_ENABLED`, each a `bool` with no default | No | `Kconfig.mbedtls:109`, `:112`, `:144`, `config-mbedtls.h:69-99` | Verified by reading |
| DTLS (`MBEDTLS_SSL_PROTO_DTLS`) | `CONFIG_MBEDTLS_SSL_PROTO_DTLS`, selected only by `NET_SOCKETS_ENABLE_DTLS` | No | `Kconfig.mbedtls:42`, Zephyr `subsys/net/lib/sockets/Kconfig:171-174`, `config-mbedtls.h:60-64` | Verified by reading |
| Renegotiation, PKCS #7 | never: neither appears in `config-mbedtls.h` | n/a | `grep` of `config-mbedtls.h` for `RENEGOTIATION` and `PKCS7_C`: no match | Verified |
| Extended master secret | `CONFIG_MBEDTLS_SSL_EXTENDED_MASTER_SECRET`, a `bool` with no default, depends on TLS 1.2 | No | `Kconfig.mbedtls:206`, `config-mbedtls.h:146-148` | Verified by reading |
| NIST curve fast reduction (`MBEDTLS_ECP_NIST_OPTIM`) | `CONFIG_MBEDTLS_ECP_NIST_OPTIM`, a `bool` with **no default at v4.4.2** | No | `Kconfig.tf-psa-crypto:21-23`, `config-tf-psa-crypto.h:67-69` | Verified by reading |
| ChaCha20 and ChaCha20-Poly1305 | `CONFIG_MBEDTLS_CIPHER_ALL_ENABLED` selects them, and the profile's cipher suite symbols name AES-GCM only | `CIPHER_ALL_ENABLED` not set | `Kconfig.tf-psa-crypto:27-37` | Verified by reading |
| Everest ECDH variant (`MBEDTLS_ECDH_VARIANT_EVEREST_ENABLED`) | no Kconfig symbol found | n/a | `grep` of `Kconfig.mbedtls`, `Kconfig.tf-psa-crypto`, `config-mbedtls.h`, `config-tf-psa-crypto.h` for `EVEREST`: no match | Verified for those four files. Whether the module's CMake defines it: **Not verified** |
| X.509 certificate parsing | selected by the ephemeral TLS 1.3 mode | Yes, through `...EPHEMERAL_ENABLED` | `Kconfig.mbedtls:153-156` | Verified by reading |

### 3.3 What Crux calls in PSA

| Call site | PSA algorithm or key type | Evidence | Status |
| --- | --- | --- | --- |
| Credential store | AES-GCM (device key), SHA-256, ECDSA P-256 and Ed25519 key pairs (the latter not supported by TF-PSA-Crypto at this pin, see `docs/psa-crypto-qualification.md`) | `components/credential/ports/psa/credential_psa.c:53-61`, `:96`, `:148`, `:213-227` | Verified |
| TLS certificate | SHA-256, ECDSA P-256 sign | `components/tls_certificate/ports/psa/tls_certificate_psa.c:28-49` | Verified |
| Access | PBKDF2-HMAC-SHA-256, HMAC-SHA-256 | `components/access/ports/psa/access_psa.c:41`, `:79-86` | Verified |
| Web sessions | SHA-256 | `components/web/ports/psa/web_session_psa.c:30` | Verified |
| Composition's PSA requests | SHA-256, HMAC, PBKDF2-HMAC, GCM, AES, ECDSA, P-256 key import, export and generate | `tests/native/management_composition/profile-psa.conf:1-12` | Verified |
| No RSA, no ChaCha20, no X25519 call | `grep` of `components/` for `PSA_ALG_RSA`, `PSA_KEY_TYPE_RSA`, `CHACHA`, `MONTGOMERY`, `X25519` and `PSA_ALG_ECDH`: no match, and no `PSA_WANT` symbol for them in the files above | Verified |
| Certificates are built and checked by Crux's own X.509 code before Mbed TLS sees them, and an installed certificate is "checked for format and for a match with the device key only", by an authenticated caller | `components/tls_certificate/src/tls_x509.h:45`, `components/tls_certificate/include/crux/tls_certificate.h:10-17`, `:167-179` | Verified |

### 3.4 The limit of this reading

The repository has no resolved `.config` of the HTTPS image on the Mbed profile. The committed `config.txt` files of the Mbed runs (`docs/evidence/tls-offload-probe/mbed/config.txt`, `docs/evidence/native-composition/mbed/config.txt`) hold a subset of symbols. I therefore derived the table of section 3.2 from the Kconfig and header text at the Zephyr tag plus Crux's own configuration files. Another symbol could `select` or `imply` one of these (**Not verified**), and the [plan](#plan-for-the-pin-move-evidence) reads the real `.config` from the CI build.

## 4. The advisories

"Needs" is the upstream condition. "Applies" is **no** when the evidence of section 3 shows the condition is not met, **cannot tell** when it does not, and **yes** when met. Severities and CVE numbers are as the upstream advisories state them. The verdict column uses the guide's three: **take**, **defer**, **not applicable**. Every verdict "not applicable" holds only while the configuration is as read.

| # | Advisory (severity) | Needs | Crux evidence | Applies | Verdict |
| --- | --- | --- | --- | --- | --- |
| 1 | Extended master secret calculation failure ignored, CVE-2026-50581 (LOW) | TLS 1.2 with the EMS extension | TLS 1.2 refused by the listener (3.1), EMS symbol not set (3.2) | no | not applicable |
| 2 | Ignored TLS 1.3 resumption secret derivation error, CVE-2026-50640 (LOW) | A TLS 1.3 server issuing session tickets | tickets not defined (3.2) | no | not applicable |
| 3 | TLS 1.3 early data integrity failure across a key change, CVE-2026-73096 (MEDIUM) | `MBEDTLS_SSL_EARLY_DATA` | defined only with a PSK mode, which Crux does not set (3.2) | no | not applicable |
| 4 | Remote buffer overflow in TLS 1.2 ECDHE-PSK client handshake, CVE-2026-50580 (HIGH) | A TLS 1.2 client with an ECDHE-PSK cipher suite and a PSK identity | server only, no PSK (3.1, 3.2) | no | not applicable |
| 5 | Information disclosure in TLS 1.2 NewSessionTicket, CVE-2026-50586 (LOW) | A TLS 1.2 server with session tickets and a failing ticket callback | TLS 1.2 refused, tickets not defined | no | not applicable |
| 6 | Out-of-bounds read in TLS 1.2 EC J-PAKE ServerKeyExchange parsing, CVE-2026-50588 (LOW) | A TLS 1.2 client with J-PAKE | no J-PAKE, no client | no | not applicable |
| 7 | Use-after-free in `mbedtls_pkcs7_free()`, CVE-2026-50579 (HIGH) | The PKCS #7 module | not defined, never called (3.2, 3.1) | no | not applicable |
| 8 | PKCS #7 signed data verification accepts weak hash algorithms, no CVE (LOW) | The PKCS #7 module | as above | no | not applicable |
| 9 | Signature algorithm restrictions not enforced on the certificate chain, CVE-2026-54441 (LOW) | An application that sets `mbedtls_ssl_conf_sig_algs` and verifies a peer's chain | Zephyr never calls `conf_sig_algs`, and the server does not verify peers (3.1) | no | not applicable |
| 10 | Incomplete context reset in `mbedtls_ssl_session_reset()`, CVE-2026-50585 (MEDIUM) | DTLS (`badmac_seen`) or DTLS-SRTP state | DTLS not compiled (3.2) | no | not applicable |
| 11 | A random generator fault can compromise TLS data integrity, CVE-2026-73064 (LOW) | A TLS 1.3 server issuing session tickets | tickets not defined | no | not applicable |
| 12 | TLS 1.3 client accepts a HelloRetryRequest selecting an unadvertised group, CVE-2026-25832 (LOW) | A TLS 1.3 client that restricts groups and connects to a malicious server | client code is compiled (3.1) but no Crux component opens a client connection and Zephyr's layer does not call `conf_groups` | no, for Crux's components | not applicable. **Not verified** for a product's own client code |
| 13 | X.509 CA bit forgery via an invalid `basicConstraints`, CVE-2026-49300 (HIGH) | Certificate validation where users can upload end-entity certificates that another validator accepts | the server does not validate peer chains. Crux does not use an installed certificate as a trust root (3.1, 3.3) | no | not applicable |
| 14 | RSA PKCS #1 v1.5 decryption timing side channel, CVE-2026-50587 (MEDIUM) | RSA decryption | no RSA in the profile or in Crux's calls (3.3) | no | not applicable |
| 15 | ChaCha20 counter overflow keystream reuse, CVE-2026-50584 (MEDIUM) | ChaCha20 and more than 256 GB under one key and nonce | ChaCha20 not selected (3.2) | no | not applicable |
| 16 | Side channel leak in ECC optimized modp, CVE-2026-54435 (MEDIUM) | `MBEDTLS_ECP_NIST_OPTIM` and a privileged local attacker with precise traces | at Zephyr v4.4.2 the symbol has no default, and Crux does not set it (3.2). **Zephyr's `v4.4-branch` turned it on by default on 11 October 2026** (commit `a92c7061e`), so a later Zephyr glue would make it apply | no, at v4.4.2 | not applicable now. Watch item for a baseline move. The candidate pin carries the fix |
| 17 | Everest: lack of contributory behaviour, CVE-2026-54434 (MEDIUM) | `MBEDTLS_ECDH_VARIANT_EVEREST_ENABLED` and X25519 | no symbol found (3.2), no X25519 in Crux (3.3), and the advisory says TLS 1.3 is not known to be exploitable | no | not applicable |
| 18 | Out-of-bounds read when parsing a zero-length ECC public key, CVE-2026-50583 (LOW) | A malformed ECC public key or a private key's optional public key reaching `mbedtls_pk_parse_key`, `mbedtls_x509_crt_parse` or a similar parser | Zephyr parses the device's own certificate and key (3.1). The input is the device's own key and an administrator-installed certificate that passed Crux's format check. Whether that check rejects an empty point was **not verified** | cannot tell | **take** |

Sources of the "Needs" column: the advisory files named in section 1, one file per row (`mbedtls-security-advisory-2026-07-<title>.md`, for example <https://github.com/Mbed-TLS/mbedtls-docs/blob/main/security-advisories/mbedtls-security-advisory-2026-07-x509-ca-bit-forgery-invalid-basicconstraints.md>).

**Count.** 16 not applicable, 1 not applicable now with a watch item (16), 1 take (18).

**What would turn a "not applicable" into "applies".** Lowering the listener to TLS 1.2 (rows 1, 5, 6 and EMS), enabling session tickets or a PSK mode (2, 3, 11), enabling DTLS (10), enabling client-certificate authentication or using Mbed TLS to validate certificates (9, 13), enabling PKCS #7 (7, 8), enabling RSA or ChaCha20 (14, 15), a TLS client in a product (4, 6, 12), Zephyr's `ECP_NIST_OPTIM` default (16). A change of any of these needs a new review.

## 5. Which revisions hold the fixes

For each advisory that lists fix commits for the 4.1.x or 1.1.x branch, the table counts the listed hashes (both ends of a range) that are an ancestor of each pin. Method and limits: section 2. Raw output: not kept in the repository, because the check is cheap to repeat with the [guide's commands](dependency-review.md#check-that-a-revision-holds-a-fix).

| Advisory | Module | Hashes tested | In the current pin | In the candidate |
| --- | --- | --- | --- | --- |
| 1 EMS | `mbedtls` | 3 | 0 | 3 |
| 2 TLS 1.3 resumption secret | `mbedtls` | 2 | 0 | 2 |
| 3 early data | `mbedtls` | 6 | 0 | 6 |
| 4 ECDHE-PSK client | `mbedtls` | 2 | 0 | 2 |
| 5 NewSessionTicket | `mbedtls` | 4 | 0 | 4 |
| 6 EC J-PAKE | `mbedtls` | 2 | 0 | 2 |
| 7 PKCS #7 use-after-free | `mbedtls` | 2 | 0 | 2 |
| 8 PKCS #7 weak hash | `mbedtls` | 2 | 0 | 2 |
| 9 signature algorithm restrictions (documentation commits only) | `mbedtls` | 2 | 0 | 2 |
| 10 session reset | `mbedtls` | 3 | 0 | 3 |
| 11 random generator fault | `mbedtls` | 3 | **1** | 3 |
| 12 HelloRetryRequest group | `mbedtls` | 2 | 0 | 2 |
| 13 X.509 CA bit | `mbedtls` | 4 | 0 | 4 |
| 14 RSA PKCS #1 v1.5 timing | `tf-psa-crypto` | 3 | 0 | 3 |
| 15 ChaCha20 counter | `tf-psa-crypto` | 8 | 0 | 8 |
| 16 ECC optimized modp | `tf-psa-crypto` | 5 | 0 | 5 |
| 17 Everest | `tf-psa-crypto` | 3 | 0 | 3 |
| 18 zero-length ECC public key | `tf-psa-crypto` | 3 | 0 | 3 |

**What this establishes.** Ancestry of a listed fix commit in the candidate, for all 18, and in the current pin for none (one commit of advisory 11 is older and is in both). It also confirms the other direction: the current pin contains the v4.1.0 tag commit but not the 4.1.1 changes, as [ADR-0006 section 2.2](decisions/0006-cryptographic-dependency-policy.md#22-how-zephyrs-commits-relate-to-the-upstream-tags) found.

**What it does not establish.** That a fix is complete (upstream says the listed commits "may not provide a complete fix" outside a release), that every commit inside a range is present, and that the candidate builds and runs under Zephyr v4.4.2's glue. It does not rest on the date of Zephyr's commit.

**The glue question.** Crux would pair module revisions from Zephyr's `v4.4-branch` with Zephyr's `v4.4.2` tag. Zephyr's own backport changed only `west.yml` (commit `9b9a84fc8`), and I did not see a Zephyr commit that the new revisions require. After v4.4.2 the branch also changed `modules/mbedtls/Kconfig.tf-psa-crypto` (the `ECP_NIST_OPTIM` default, 11 October 2026, which Crux's v4.4.2 does not have), `drivers/crypto/crypto_mbedtls_shim.c` and `modules/openthread/platform/crypto_psa.c` (the compare returned 300 of the branch's changed files, so the list can be incomplete). Whether the 4.1.1 and 1.1.1 modules need any of them was **not verified**. The CI run of the [plan](#plan-for-the-pin-move-evidence) is the test.

## 6. Decision of this review

- **Take** the pin move to `098e120a` and `fa923495`, as the first exercise of ADR-0006's security-fix path. It stays within the 4.1 and 1.1 lines, uses revisions that Zephyr pins on a release branch, and carries the fixes for all 18 advisories.
- **Not an emergency:** no advisory is known to be reachable in the configuration as read, the one that might be (row 18) needs an authenticated administrator and is rated LOW, and the exposure on a configuration change is real, so the move should not wait for an undated Zephyr tag.
- **Defer nothing.** The only watch item is row 16, which becomes relevant when a Zephyr baseline moves.
- **This record approves nothing.** The pin move needs the owner's approval of the move itself, after this review. The protected change to `scripts/test-psa-crypto.sh` needs its own approval.

## What could not be established

- A resolved `.config` of the HTTPS image on the Mbed profile (section 3.4). Another symbol may select or imply a feature that I read as off.
- Whether Crux's certificate installation rejects a zero-length EC point (row 18).
- Whether the module CMake defines `MBEDTLS_ECDH_VARIANT_EVEREST_ENABLED` (row 17).
- Whether a product's own code uses a TLS client, PKCS #7, RSA or ChaCha20 (rows 4, 6, 7, 8, 12, 14, 15). This review covers `components/` and the Mbed profile files.
- The 11 March 2026 advisories (section 1).
- Whether the candidate revisions work with Zephyr v4.4.2's glue (section 5).
- The completeness of each upstream fix and every commit inside a range (section 5).
- Whether the upstream advisories are complete: I read the 7 July set that the two releases list.

## Plan for the pin move evidence

This is a plan only. Nothing here was run, and the move is not approved. It implements item 4 of [ADR-0006 option B](decisions/0006-cryptographic-dependency-policy.md#3-policy-options). The evidence runs in CI because local runs are not planned: disk space is at about 12 GB and Docker is in use for other work.

| Step | What | Where | Pass means |
| --- | --- | --- | --- |
| 1 | The pin base check of the guide on both candidate revisions: version strings 4.1.1 and 1.1.1, `ahead_by` and `behind_by` against the upstream tags | Any shell with `gh`, or the workspace checkout | `behind_by` 0, version strings as expected, counts recorded in the PR |
| 2 | The pin consistency check, which compares `west.yml`, `cmake/CruxPsaCrypto.cmake`, Zephyr's manifest and the checkout | `scripts/test-psa-crypto.sh host tf-psa-crypto`, CI step at `.github/workflows/psa-crypto.yml:64-66` | **Fails today by design for the new pins**: `scripts/test-psa-crypto.sh:93-97` rejects a pin that differs from Zephyr's manifest at v4.4.2. The protected change must let a declared security pin pass, naming the allowed revisions. It is a separate approval |
| 3 | The `cmake/CruxPsaCrypto.cmake` revision and SHA-256 of the archive for `fa923495`, computed from the downloaded archive | PR author | the same revision as `west.yml` |
| 4 | Host and `native_sim` PSA conformance on TF-PSA-Crypto | `psa-crypto.yml:64-70` (`host`, `zephyr` with the entropy declaration) | the summary line `N cases, 0 failed, X declared unsupported, Y declared deviations` equals the recorded counts, or each difference is explained (a gap that closes or opens is a finding) |
| 5 | The Mbed-profile composition on `native_sim`: the TLS socket probe, the management composition, the web adapter over HTTPS and the login and session suite | `psa-crypto.yml:84-98` (`test-tls-offload-probe.sh mbed`, `test-native-composition.sh mbed`, `test-web.sh https mbedtls`, `test-web-session.sh native`) | all steps pass, and `docs/https-composition.md`'s declared cases (TLS 1.3 only, the one suite) still hold |
| 6 | RT1060 compile of the conformance application, the web adapter and the PSA ports, without running them | `psa-crypto.yml:72-78`, `:100-102` | compiles and links |
| 7 | Read the resolved `.config` of the HTTPS image from the CI log or artifact, for the symbols of section 3.2: `MBEDTLS_SSL_SESSION_TICKETS`, the PSK and J-PAKE key exchanges, `MBEDTLS_SSL_PROTO_DTLS`, `MBEDTLS_ECP_NIST_OPTIM`, `MBEDTLS_CIPHER_ALL_ENABLED` and the PSA symbols for RSA, ChaCha20 and X25519 | the job's uploaded logs (`psa-crypto.yml:104-108`) | each reads `n` or absent, so that rows 1 to 17 hold, and the result is recorded |
| 8 | Update every place that names the two commits: `tests/target/bench_image.py:44-45`, `docs/rt1060-bench-image-plan.md:197-198`, `docs/upstream-reuse.md:41-42`. `docs/psa-crypto-qualification.md:20-21` and `docs/rt1060-management-image-feasibility.md:493` keep the old pins as history | PR author | a `grep` of the maintained tree for the old commits finds only history |
| 9 | A certificate-install check for row 18, if the owner wants the open point settled: whether `tls_x509_parse` rejects an empty EC point | a Host test under `tests/` (not protected), written test-first | the case passes, or the finding is recorded |
| 10 | Independent review by the `reviewer` agent, the owner's approval of the move, and the release notes line | PR | PASS, approval quoted with time and place |

**Not part of the plan.** A physical-board run: the board evidence of `docs/evidence/psa-crypto/rt1060-board-2026-11-07/` belongs to the old pin and is not carried over, and ADR-0006 does not require a new one. A board run needs its own approval.

## What was run

On native macOS, in the worktree at `8b48e331`: `git fetch`, reads of the files cited above, `grep`, `gh api` and `curl` against public GitHub URLs (releases, tags, contents, compare and the advisory files), all on 14 November 2026. The Zephyr v4.4.2 files were read through the GitHub contents API. Not run: `west`, CMake, any build or test, Docker, any hardware command, any proof-of-concept code. The documentation link check is recorded in the pull request.

## Record

| Item | Value |
| --- | --- |
| Baseline | `8b48e33175031a0b9d32876789b03f5eb3d4f997` |
| Skills and agents used | None that produced a result. `ecc:security-review` and `ecc:deep-research` were considered and not used (see section 2) |
| Model | `claude-sonnet-5-5`, Claude Code |
| Independent review | The `reviewer` agent, verdict in the pull request |
| Approval | None. The owner's approval of the pin move itself, and of the `scripts/test-psa-crypto.sh` change, are still needed |
