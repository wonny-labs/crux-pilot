# Secure management provider decision

Status: recommendation for owner approval, Issue #176. Build-only evidence from 3 November 2026. Nothing was
flashed or run, so no result here shows that a handshake, login or transfer works. The owner approved the move to
Zephyr v4.4.2, merged in #199. Adding the stacks to the manifest needs the owner's approval and a separate change.

## Recommendation

1. Stay on Zephyr v4.4.2 now, and plan a re-baseline to 4.6 LTS when it is released and a wolfSSL socket patch exists
   for it. Do not move to 4.5.0.
2. Keep the Wolf profile as the first complete profile: wolfSSL 5.9.4, wolfSSH 1.6.0 and, where PSA Crypto is needed,
   wolfPSA 5.9.4, using the wolfSSL Zephyr socket patch.
3. Keep the Mbed profile for HTTPS seam validation only.
4. Do not use Zephyr SSH for now.
5. Use the pilot credential storage in the section below, and do not describe it as confidential.

## Inputs

| Input | Revision |
| --- | --- |
| Zephyr | v4.4.2 `dccb095` |
| `hal_nxp` | `7554bc0` |
| Mbed TLS, TF-PSA-Crypto | `a3e190f`, `dc575a2`, unchanged from v4.4.0 |
| wolfSSL | v5.9.4-stable `3c5eead` |
| wolfSSH | v1.6.0-stable `8221e4e` |
| wolfPSA | v5.9.4 `55ae0ec` |
| wolfSSL Zephyr patches | `wolfSSL/osp` `8c32b06`, files for 4.4.1 |
| Toolchain | Zephyr SDK 1.0.1, Dev Container image |

## Rejected options

| Option | Reason |
| --- | --- |
| Mbed TLS for SSH | It has no SSH implementation |
| Mbed profile as the first profile | Its current maintainers stop work on the TLS and X.509 libraries from April 2027 unless new maintainers take over |
| Zephyr SSH | At v4.5.0 it is experimental, has no SFTP server, one account per server, RSA host keys only, no strict key exchange code in its core sources, and needs v4.5, for which no wolfSSL patch exists. Upstream may improve, so it stays a possible later provider. |
| libssh, Dropbear, CycloneSSH | Not evaluated beyond documents. libssh has no confirmed Zephyr port, Dropbear expects `fork`, and CycloneSSH would add a further commercial or GPLv2 dependency without a need shown. |
| Zephyr v4.5.0 now | All three patches fail, and it would need far more re-validation |

## Zephyr baseline

Each patch was first checked with `git apply --check` against the files at each tag.

| Tag | wolfSSL TLS socket patch | wolfSSL decoupling patch | Crux MDIO patch |
| --- | --- | --- | --- |
| v4.4.0 | Fails in `sockets_tls.c` | Applies | Applies |
| v4.4.1 | Applies | Applies | Applies |
| v4.4.2 | Direct apply fails in `sockets_tls.c`. A 3-way merge succeeds with no conflict. | Applies | Applies |
| v4.5.0 | Fails: 14 errors | Fails | Fails |

For v4.4.2, upstream added 33 lines to `sockets_tls.c` between v4.4.1 and v4.4.2 (24 non-blank) and removed 8. An
independent comparison found that the merged file differs from the patched v4.4.1 file by exactly those upstream
hunks, so no wolfSSL logic was dropped, and the builds below compile it. wolfSSL has published no patch for v4.4.2 or v4.5.

Moving to v4.5.0 would mean porting all three patches and re-validating far more of the evidence from M1 to M6.
Nordic's release table lists the end of life of 4.4 as 12 April 2027 and projects 4.6 as LTS4 in April 2027. Both come
from a downstream table and may change. A product with a long support period should build on an LTS release, so 4.4.2
is an interim step.

## Build results

Target `mimxrt1060_evk@B//qspi`, Zephyr v4.4.2. Values come from the linker memory report. RAM includes configured
stacks and heaps, so it is not measured use. All HTTP builds use the Zephyr HTTP sample.

| Build | Flash | RAM | Configured memory included in RAM | `mbedtls_` symbols |
| --- | --- | --- | --- | --- |
| HTTP, no TLS, no WebSocket | 268 KB | 128 KB | | 0 |
| HTTPS, Mbed TLS, no WebSocket | 339 KB | 206 KB | 60,000-byte Mbed TLS heap | 363 |
| HTTPS, wolfSSL, no WebSocket | 486 KB | 202 KB | 64 KB allocation arena | 0 |
| HTTPS, wolfSSL and wolfPSA with the decoupling patch, with WebSocket | 495 KB | 211 KB | 64 KB allocation arena | 0 |
| wolfSSH server and SFTP probe with a TLS socket, sharing wolfSSL | 361 KB | 215 KB | 32 KB main stack, 128 KB allocation arena | 0 |

The probe calls server, stream and SFTP entry points. It has no HTTP server, so these figures do not add up to a full
image, and Issue #197 measures that. TLS adds about 71 KB of Flash with Mbed TLS and about 218 KB with wolfSSL here, but
the comparison is uneven. The Mbed TLS overlay enables a restricted cipher configuration, and wolfSSL used its untrimmed
module defaults. Trimming should reduce the wolfSSL figures, and the amount is unmeasured.

`native_sim/native/64` results:

| Build | Result |
| --- | --- |
| HTTPS, wolfSSL | Builds on v4.4.2 |
| HTTPS, Mbed TLS | Built on v4.4.1, and not repeated on v4.4.2 |
| wolfSSH probe with `CONFIG_POSIX_API=y` | Fails on v4.4.1 and v4.4.2: `sys/_timeval.h` is missing with the host C library |
| wolfSSH probe with `CONFIG_POSIX_API=n` and `CONFIG_POSIX_SYSTEM_INTERFACES=y` | Built on v4.4.1, and not repeated on v4.4.2 |
| wolfSSH probe with a TLS socket and `CONFIG_POSIX_TIMERS=y`, without `CONFIG_POSIX_API` | Fails on v4.4.2: `posix_signal.h` needs `union sigval`, which depends on `_POSIX_REALTIME_SIGNALS`. `CONFIG_POSIX_API` selects that and `CONFIG_POSIX_TIMERS` does not, so a Kconfig gap is likely, and a fix is untested. |

## Findings for later work

- **Combined image on `native_sim`.** No tested configuration builds wolfSSH with the wolfSSL socket backend on
  `native_sim`. M12's plan to test SFTP with OpenSSH on `native_sim` depends on solving this, or on testing SSH without
  the socket backend. Issue #205 decided this: see [the SSH provider contract](../ssh-provider-contract.md#testing-on-native_sim).
- **wolfSSL settings must suit both consumers.** The socket backend requires Kconfig-driven wolfSSL settings, so it
  rejects `CONFIG_WOLFSSL_SETTINGS_FILE`. The module's default `user_settings.h` then defines `NO_FILESYSTEM`
  unconditionally. In `wolfssh/port.h` that takes priority over the file system ports other than
  `WOLFSSH_FATFS`, including `WOLFSSH_USER_FILESYSTEM`, so SFTP does not compile. The combined probe built only after that line was removed in a
  scratch copy, and it also added definitions such as `WOLFSSL_WOLFSSH`. Issue #205 decided the arrangement: see
  [the SSH provider contract](../ssh-provider-contract.md#wolfssl-and-wolfssh-settings).
- **wolfPSA needs the Mbed modules in the workspace.** The PSA Kconfig tree lives under Zephyr's Mbed TLS module, so
  `mbedtls` and `tf-psa-crypto` must be present for configuration, even though no `mbedtls_` symbol is linked. Without
  them, `PSA_CRYPTO_PROVIDER_CUSTOM` has no effect and configuration fails.
- **WebSocket needs a PSA provider.** `HTTP_SERVER_WEBSOCKET` selects PSA hash support. The wolfPSA build shows that this
  configures and links. The web design polls, so M11 does not need it at first.
- **The sample needed a change.** The Zephyr HTTP sample's `NET_SAMPLE_HTTPS_SERVICE` selects `PSA_CRYPTO`. That line was
  removed in a scratch copy for the wolfSSL build without PSA. It is a sample setting and does not affect a Crux composition.
- **Execution profile.** The wolfSSL target builds use `CONFIG_POSIX_API=y`, and Issues #184 and #192 check this against
  the Crux execution profile.
- **Mbed TLS maintenance.** The announcement that maintainers stop working on the TLS and X.509 libraries from April 2027
  was checked through a quoted copy. This is why Mbed is not the first profile.

## Credential and key storage on RT1060-EVKB

This section compares where the account store, TLS private key and SSH host key could live. It is a
source reading of Zephyr v4.4.2, `hal_nxp` and the Crux documents. No hardware was operated and no
fuse or boot configuration was read, so every hardware statement below is unverified.

### What the source shows

| Topic | Finding |
| --- | --- |
| Entropy | `nxp_rt10xx.dtsi` sets `zephyr,entropy = &trng` for the `nxp,kinetis-trng` block at `0x400cc000`, and `ENTROPY_MCUX_TRNG` defaults to `y` when that node is enabled. This identifies the entropy source for Issue #142, and its quality and start-up behaviour are not evidenced. |
| Device ID | `HWINFO_IMXRT` reads the ID from the OCOTP fuse shadow registers. This value is readable by software and by anyone who can read the chip, so it is an identifier and not a secret. |
| Zephyr secure storage | `subsys/secure_storage` encrypts entries through a transform. The stock AEAD transform and key providers call Mbed TLS internals, so they do not fit the Wolf profile. Its default key provider hashes the hwinfo device ID, and its help text says this is "not necessarily secure". |
| wolfPSA storage | wolfPSA ships a custom AES-256-GCM transform, with the key derived as SHA-256 of the device ID and entry UID. Without `CONFIG_HWINFO`, it falls back to a fixed constant, so a composition must require hwinfo. Its documentation says this is functional protection only, and it has no replay protection. It has its own tests, not run here. |
| Silicon features | `hal_nxp` includes drivers for DCP, BEE, SNVS and OCOTP, and Zephyr has a DCP crypto driver. Whether a hardware-bound key can be used on this board without programming fuses or closing the boot configuration is not shown here and needs NXP documentation. |
| Boot and fuse state | The M1 readiness assessment says the boot path is not an authenticated chain and that fuse and lock state were not assessed. The plan keeps fuse programming and closed configurations outside pilot work. |
| Writable media | M4 selected microSD with FatFs for writable data, and writable data on QSPI NOR is outside that path unless reviewed separately, because Flash writes during XIP execution are still under investigation (Issues #141 and #165). M3 found no confirmed populated EEPROM on the EVKB. |

### Options

| Option | Medium | Key | Confidentiality claim | Integrity claim | Status |
| --- | --- | --- | --- | --- | --- |
| A | microSD through the File or Settings path | Transform with a device-ID key: wolfPSA's for the Wolf profile, the stock one for Mbed | None against someone who can read the card and the chip ID. It stops only casual reading of the card. | AEAD detects accidental corruption. Someone who knows the key can replace entries or restore an old copy. | Possible once M4 storage is accepted, and its integration is untested |
| B | QSPI NOR `storage_partition` | Same as A | Same as A, with no removable medium | Same as A | Blocked on Flash-write safety work |
| C | Either medium | A custom transform (Wolf) or key provider (Mbed) backed by a hardware-bound key | Device-bound if the hardware key path is proven | Stronger than A, if the key cannot be read | Needs fuse and boot decisions, so it is a product decision |
| D | RAM only | None | Lost at reset | Lost at reset | Fits session tokens only, and cannot hold accounts |

### Recommendation

1. Define the credential and key store interface so that the protection behind it can be replaced, as in
   Issue #178, and Option C can be added without changing callers.
2. Use Option A for pilot evidence, and state its claim as follows: stored secrets are readable by
   anyone who can read the storage medium and the chip ID, and accidental corruption is detected. Do not
   describe it as confidential storage. The store must also meet the access contract's requirements: it survives
   power loss with the old or new record, reports `EMPTY`, `VALID` or `UNUSABLE`, and stays out of reach of
   management file areas. A failed authentication tag is `UNUSABLE`.
3. Treat password verifiers differently from private keys. A salted, slow verifier resists offline
   guessing, so its main risk is replacement by someone who can write to storage. Private keys need
   confidentiality, so Option A gives them no protection against that attacker.
4. Do not use Option B until Flash-write safety is decided. Treat microSD as a Pilot choice: a removable
   medium is a poor home for secrets in a product, and removing it also blocks logins.
5. Leave Option C to the product, and record that its fuse operations are outside this work.

This leaves the CRA-related question open: whether this protection level is acceptable depends on the
product's threat model, such as physical access, and compliance owns that decision.

## Not done

wolfPSA was built only for the RT1060 target here. Since then, Issue #142 has run wolfPSA on Linux Host and `native_sim` with
[the PSA conformance cases](../psa-crypto-qualification.md). Stack use is unmeasured, and so is a full image's footprint, which Issue #197 covers. Handshake time and verifier cost
need hardware approval. Whether a low-cost evaluation kit can represent other boot
media or execution from external RAM was not studied here.

## Licences

wolfSSL and wolfSSH are offered under GPLv3 or a commercial licence. wolfPSA's source states GPL-3.0-or-later only. A vendor
article says it has the same licence options as wolfSSL, and its commercial terms need confirming with the vendor. Crux source stays Apache-2.0, and these stacks
are fetched by west and are not copied into this repository. A binary built with a GPLv3 stack is subject to GPLv3 when
distributed, so Pilot images with the Wolf profile are for evaluation and are not published. New files in the wolfSSL Zephyr
patches carry an Apache-2.0 header. The `wolfSSL/osp` repository has no licence file, and its README says that ports of other
projects follow each project's own licence. It does not name this patch, so confirm the terms before storing it in this repository. A product needs its own commercial licence, and its scope should cover each stack that it
uses. This is an engineering summary and needs legal review for any product.
