# Secure management provider decision

Status: recommendation for owner approval, Issue #176. Build-only evidence from 3 November 2026.
Nothing was flashed or run, so no result here shows that a handshake, login or transfer works. This
record adopts no dependency. Moving the Zephyr baseline and adding stacks to the manifest need the
owner's approval and a separate change.

## Recommendation

1. Move the Zephyr baseline to the 4.4 branch now, and plan a re-baseline to 4.6 LTS when it is
   released and a wolfSSL socket patch exists for it. Do not move to 4.5.0.
2. Prefer Zephyr 4.4.2 with a small rebase of the wolfSSL patch. Use 4.4.1 if the rebase proves larger
   than the three rejected hunks suggest.
3. Keep the Wolf profile as the first complete profile: wolfSSL 5.9.4, wolfSSH 1.6.0 and, where PSA
   Crypto is needed, wolfPSA 5.9.4.
4. Keep the Mbed profile for HTTPS seam validation only.
5. Do not use Zephyr SSH for now.

## Zephyr baseline

Each patch was checked with `git apply --check` against the files at each tag.

| Tag | wolfSSL TLS socket patch | wolfSSL decoupling patch | Crux MDIO patch |
| --- | --- | --- | --- |
| v4.4.0 (pinned) | Fails in `sockets_tls.c` | Applies | Applies |
| v4.4.1 | Applies | Applies | Applies |
| v4.4.2 | Fails: three hunks in `sockets_tls.c` | Applies | Applies |
| v4.5.0 | Fails: 14 errors | Fails | Fails |

The rejected 4.4.2 hunks are in the client session cache region of one file, and they were not rebased
here. The 4.4 branch has 327 commits between v4.4.0 and v4.4.2, while v4.4.0 to v4.5.0 is at the
10,000-commit limit of the GitHub compare API. Moving to 4.5.0 would mean porting all three patches
and re-validating far more of the evidence from M1 to M6.

Nordic's release table lists the end of life of 4.4 as 12 April 2027. It also projects 4.6 as LTS4 in
April 2027. Both come from a downstream table and may change. A product with a long support period
should build on an LTS release, so the 4.4 move is an interim step.

The move changes `hal_nxp` from v4.4.0's revision to `7554bc0f`. The `mbedtls` and `tf-psa-crypto`
revisions stay the same. Existing target evidence applies to v4.4.0 and must be re-established where
the changed code matters.

## Build results

Target `mimxrt1060_evk@B//qspi`, Zephyr v4.4.1, Zephyr SDK 1.0.1. Values come from the linker memory
report. RAM includes configured stacks and heaps, so it is not measured use.

| Build | Flash | RAM | Configured memory included in RAM |
| --- | --- | --- | --- |
| HTTP sample, no TLS, no WebSocket | 267 KB | 128 KB | |
| HTTP sample, Mbed TLS, no WebSocket | 338 KB | 206 KB | 60 KB Mbed TLS heap |
| HTTP sample, wolfSSL, no WebSocket | 485 KB | 202 KB | 64 KB allocation arena |
| wolfSSH server and SFTP probe | 238 KB | 206 KB | 32 KB main stack, 128 KB allocation arena |
| wolfSSH probe with a TLS socket on wolfSSL | 417 KB | 215 KB | 32 KB main stack, 128 KB allocation arena |

The probe calls server, stream and SFTP entry points. It has no HTTP server, so these figures do not
add up to a full image. Issue #197 measures that.

wolfSSL adds about 147 KB more Flash than Mbed TLS here, but the comparison is uneven. The Mbed TLS
overlay enables a restricted cipher configuration, and wolfSSL used its untrimmed module defaults. The
SSH probe uses the test settings shipped with wolfSSH, which include SCP. Trimming is expected to
reduce all wolfSSL figures, and the amount is unmeasured.

`native_sim/native/64` results:

| Build | Result |
| --- | --- |
| HTTP sample, Mbed TLS | Builds |
| wolfSSH probe with `CONFIG_POSIX_API=y` | Fails: `sys/_timeval.h` is missing with the host C library |
| wolfSSH probe with `CONFIG_POSIX_API=n` and `CONFIG_POSIX_SYSTEM_INTERFACES=y` | Builds |
| wolfSSH probe with `CONFIG_PICOLIBC=y` | Fails with the same error |
| HTTP sample, wolfSSL | Not retried after the sample fix below |

## Findings for later work

- **wolfSSL settings must suit both consumers.** The socket backend requires Kconfig-driven wolfSSL
  settings, so it rejects `CONFIG_WOLFSSL_SETTINGS_FILE`. The module's default `user_settings.h` then
  defines `NO_FILESYSTEM` unconditionally. In `wolfssh/port.h` that takes priority over every file
  system port, including `WOLFSSH_USER_FILESYSTEM`, so SFTP does not compile. The combined probe built
  only after that one line was removed from a scratch copy. Issue #192 must find a maintainable answer,
  either an upstream option or a Crux settings arrangement. One wolfCrypt for both TLS and SSH is
  therefore shown to link, but only with this change.
- **WebSocket needs a PSA provider.** `HTTP_SERVER_WEBSOCKET` selects PSA hash support, which fails
  without Mbed TLS or a PSA provider. The web design polls, so this does not affect M11 at first. A later
  WebSocket feature needs wolfPSA in the Wolf profile.
- **The sample needed a change.** The Zephyr HTTP sample's `NET_SAMPLE_HTTPS_SERVICE` selects
  `PSA_CRYPTO`. That line was removed in a scratch copy for the wolfSSL build. It is a sample setting and
  does not affect a Crux composition.
- **Execution profile.** The wolfSSL builds use `CONFIG_POSIX_API=y` on the target. Issue #184 and Issue
  #192 check this against the Crux execution profile.
- **wolfPSA was not built.** It is needed by the Wolf profile when Crux code calls PSA Crypto, so Issue
  #142 covers it.

## Not done

Issue #176 also asks for a comparison of credential storage options on RT1060-EVKB. That analysis is
not in this record and needs a follow-up before gate G1. The upstream question on Mbed TLS maintenance
was checked through a quoted copy of the announcement.

## Licences

wolfSSL, wolfSSH and wolfPSA are offered under GPLv3 or a commercial licence. Crux source stays
Apache-2.0, and these stacks are fetched by west and are not copied into this repository. A binary
built with a GPLv3 stack is subject to GPLv3 when distributed, so Pilot images with the Wolf profile are
for evaluation and are not published. A product needs its own commercial licence, and its scope should
cover each stack that it uses. This is an engineering summary and needs legal review for any product.
