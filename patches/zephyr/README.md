# Reference-controller native MDIO correction

`nxp-enet-mdio-completion.patch` is an opt-in correction for the single-core
RT1062 reference composition. It applies only to
`drivers/ethernet/mdio/mdio_nxp_enet.c` at Zephyr
`dccb09599635bdff17633fa7e9dab014b91dce90`. The original file is NXP copyright,
Apache-2.0 licensed; its notice remains in the generated copy. The patch is
maintained here pending an upstream resolution, without a dependency upgrade.

The original wait discards the semaphore timeout result and can return a
successful zero read without completion. An injected old token also demonstrates
an early successful read. These are source-level logical counterexamples; the
initial cause of the observed physical failure has not been established.

The correction:

- Requires a real completion indication during initial polling.
- Clears the old event and semaphore before issuing a new command, with the
  single-core IRQ handler excluded from that start sequence.
- Returns `-ETIMEDOUT` when completion is not received. The caller's output is
  preserved on failure.
- Rejects subsequent transactions with `-EIO` after a timeout. Only a fresh boot
  clears this state. A late IRQ, ordinary callback or module-reset notification
  cannot silently make an uncertain operation reusable.
- Rejects ISR callers before acquiring the sleeping mutex.

`CONFIG_CRUX_M3_MDIO_COMPLETION_FIX` defaults off and requires native NXP MDIO,
RT1062, no SMP and no zero-latency interrupts. Append
`samples/controller/zephyr/m3-mdio-completion.conf` to the network configuration
for a candidate build. This is not a platform API, portable driver replacement,
new timing guarantee or physical qualification claim. No timeout is increased.
Native PHY errors do not necessarily change cached carrier state: the patch
alone does not establish readiness or link-loss acceptance.

`scripts/prepare-nxp-mdio.py` checks the exact original source hash, applies the
patch in a temporary directory and verifies the generated hash. CMake replaces
exactly one source in the existing native Ethernet library with this build-local
copy. It never edits the Zephyr checkout. The generated `.json` records original,
patch and output hashes. Source drift or missing/ambiguous library membership
fails configuration; there is no dirty-dependency exception.

Run `python3 tests/native/mdio_completion/test_driver.py` in the Linux development
container with the pinned workspace initialised. It compiles the actual generated
function bodies and data structure with explicitly modelled IRQ/MMIO/semaphore
schedules, including completion during the locked start and after timeout. It
also rejects source/patch drift, output into the dependency tree and ambiguous
CMake source replacement. The model supplies W1C and interrupt effects explicitly;
it does not reproduce electrical timing, the physical IRQ seed, RTOS scheduling
or worst-case execution. Target compilation and reviewed physical evidence remain
separate gates. The ordinary profiles retain their original driver selection.


## First-failure observation

The first physical corrected candidate booted but did not reach Link readiness
within the original 15-second window. No application exchange or post-round PHY
observation ran. The failure does not identify a poisoned bus or the initial cause.
A default-off `CONFIG_CRUX_M3_MDIO_FAILURE_TRACE` adds observation only to the
PHY diagnostic composition. Append `m3-mdio-trace.conf` as well as the existing
network, PHY diagnostic and completion-correction configuration files.

The native driver retains its first wait failure in CPU-accessed BSS, publishing
a valid flag last under single-core IRQ protection. A private getter copies that
record under the same protection; it is not a public Crux interface. The record
includes the actual wait branch, operation/PHY/register, result, transaction cycle
observations, a monotonic timestamp, sequential read-only MAC fields and the native
semaphore count. It performs no extra MDIO operation or register write. Later
poison errors never overwrite the first failure or reset the failed state.

The sample stays quiet until a retained failure exists, then reports native
MDIO/PHY readiness with that one complete record through its existing output path. It owns no MDIO mutex and
prints after releasing snapshot IRQ protection. It adds no thread, logging backend,
command, retry or timing limit. Ordinary images do not compile the getter/record.
The generated native translation unit uses scoped `-Wall -Wextra -Werror`; other
upstream translation units retain their warning selection.

The cycle fields are unsigned 32-bit modular observations; multiple wraps cannot
be reconstructed. The frequency is the native counter frequency at observation.
The millisecond timestamp is also 32-bit and wraps. These clocks are available in
the selected POST_KERNEL initialisation path; this is not permission to reuse the
observer in earlier boot phases. Fields are sequential, not a simultaneous hardware
snapshot or proof of IRQ history. Diagnostic output occurs after retention and can
perturb later execution. No WCET, physical cause or corrected Link behaviour is
inferred from these observations. The unchanged physical qualification remains
required after any eventual fix.


The private `m3-deferred-output.conf` is a separate opt-in software evaluation for
that diagnostic. Native `LOG_PRINTK` queues raw records before the direct printk
spinlock path; one native UART backend performs output later. This avoids a custom
logging service but adds a finite 4096-byte buffer and 2048-byte logger stack at
priority 14. It does not change the MDIO patch, timeout or poison policy. It has
not been adopted for Ethernet or integrated qualification. Native_sim fixtures
exercise long 64-bit records and actual forced-overflow loss reporting; output
loss, incomplete records and unexpected configurations remain failures. UART wait,
IRQ dispatch and native tick rounding are relevant source hypotheses, not an
established physical cause of the earlier failure.

# wolfSSL TLS socket backend

The secure management work uses a wolfSSL backend behind the Zephyr TLS socket API. Its patch is not stored in this
repository. New files in it carry an Apache-2.0 header. The upstream repository has no licence file, and its README says
that ports of projects other than wolfSSL follow each project's own licence, which for Zephyr is Apache-2.0. It does not name
this patch, and the patch also calls wolfSSL, so Crux refers to it by revision until its terms are confirmed.

| Input | Value |
| --- | --- |
| Repository | `wolfSSL/osp`, revision `8c32b060ac6017b1291a6da0875a48637bfb682d` |
| Core patch | `zephyr/4.4/zephyr-tls-4.4.1.patch`, SHA-256 `3d1cc0307bbeea5f000698bca5be717f73fdb8c47e90083b003336364e8fdbda` |
| Decoupling patch | `zephyr/4.4/zephyr-tls-4.4.1-mbedtls-decoupling.patch`, SHA-256 `b581396c70e7ecaf4b09ae66875b8cc5d29e428fd74c9938d3249d426b185c05` |

Upstream generated these for Zephyr 4.4.1. On v4.4.2, `git apply --check` of the core patch fails in `sockets_tls.c`, and
`git apply --3way` merges it with no conflict. The result differs from the patched 4.4.1 file by exactly the upstream changes
between those tags. It runs on `native_sim` in the [TLS probe](../../docs/native-management-composition.md#tls-probe), on a patched copy of the tree that
`scripts/prepare-wolf-tls-zephyr.sh` makes for a test, and it has run on nothing else. How a product composition applies it without editing the Zephyr
checkout is decided under Issue #185. The wolfSSL, wolfSSH and wolfPSA projects are fetched only when a workspace enables the
`crux-secure-management` group. wolfSSL and wolfSSH are offered under GPL-3.0-or-later or a commercial licence. wolfPSA's source
states GPL-3.0-or-later only. A vendor article says it has the same licence options as wolfSSL, and its commercial terms need
confirming with the vendor. Binaries built with these stacks are subject to those terms when distributed. The `mbedtls` and `tf-psa-crypto` projects in the same group are
Apache-2.0 OR GPL-2.0-or-later.

## Mbed TLS entropy without a fallback

`mbedtls-entropy-fail-closed.patch` changes one line of `modules/mbedtls/zephyr_entropy.c` at Zephyr
`dccb09599635bdff17633fa7e9dab014b91dce90`: `mbedtls_platform_get_entropy` passes `false` instead of `true` for whether
a failed entropy read may fall back to `sys_rand_get()`. The file is Nordic Semiconductor copyright, Apache-2.0 licensed, and its notice remains in the
generated copy. The file has the same SHA-256 at v4.4.0 and v4.4.2 (`302e3738…`), and the patched copy has `65ed87b0…`.

Without it, a failed read on RT1060-EVKB seeds the PSA random generator from a non-cryptographic source and still reports full entropy. At v4.4.2 the TRNG
driver returns `-EIO` on a failed read, so that path is reachable. With it, Mbed TLS reports the failure and PSA does not initialise. Owner decision of 6 November 2026,
recorded in the [qualification record](../../docs/psa-crypto-qualification.md#entropy-failure).

`scripts/prepare-mbedtls-entropy.py` checks the original's hash, applies the patch in a temporary directory and checks the result's hash. `cmake/CruxMbedtlsEntropy.cmake`
replaces exactly one source in the one library that compiles it, with the build-local copy, and never edits the Zephyr checkout. Source drift, a missing library or a
second one in `modules/mbedtls` fails configuration. A compile of the file from another directory would not, and only the RT1060-EVKB conformance image checks the compile commands. It applies only on the entropy driver route (`CONFIG_MBEDTLS_PSA_DRIVER_GET_ENTROPY`). The external-RNG route does not fall back to `sys_rand_get()` unless `CONFIG_MBEDTLS_PSA_CRYPTO_EXTERNAL_RNG_ALLOW_NON_CSPRNG` is set, but it accepts whatever its entropy driver returns. It does not touch the TLS socket patch above, and does not change what an image that does not call it does.

# eMMC cache flush on disk sync

`mmc-cache-flush.patch` is an opt-in correction of the Zephyr MMC stack. It applies only to `subsys/sd/mmc.c` at Zephyr
`dccb09599635bdff17633fa7e9dab014b91dce90`. The original file is NXP copyright, Apache-2.0 licensed; its notice remains in
the generated copy.

At the pin, `mmc_card_init` turns on the eMMC cache whenever the device reports one, and `DISK_IOCTL_CTRL_SYNC` only waits
for the card to be ready. No flush command exists in the stack, so on a device with a cache a successful sync does not mean
that the data reached the medium. The eMMC simulator in `tests/support/emmc_sim` reproduces this; it has not been observed
on a device.

With the correction, `mmc_set_cache` records in a private bit of `card->flags` that it turned the cache on, after it has sent the cache-on command and waited for the card. It does not read back whether the card accepted the command, as `sdmmc_wait_ready` discards the switch error bit; if the card rejected it, an extra flush is sent, which is the safe direction. `sd_init`
clears the flags on each initialisation. When that bit is set, a sync holds the card lock and:

1. Waits for the card with `sdmmc_wait_ready`, which finishes earlier writes and clears any stale status.
2. Sends CMD6 to write `FLUSH_CACHE` (EXT_CSD byte 32) with value 1, with `CONFIG_SD_DATA_TIMEOUT` as its timeout.
3. Polls `sdhc_card_busy`, bounded by the same timeout.
4. Sends CMD13 until the card is ready in the transfer state, bounded by the same timeout.

A failure in any step returns an error: the wait's own value in step 1 (`-EBUSY` when the card stays busy), the host's
value for a failed command, `-ETIMEDOUT` for a busy period or status poll that does not end, and `-EIO` when a status
response carries the switch error bit (bit 7). Steps 3 and 4 do not use `sdmmc_wait_ready`, because its status reads
discard bit 7, which the card clears when it is read. CMD13 is sent without retries, since a lost response may have held
that bit. Without a cache, or before initialisation, the sync behaves as at the pin and sends no flush.

`CONFIG_CRUX_MMC_CACHE_FLUSH` defaults off and needs `MMC_STACK`. This is not a platform API, a driver replacement or a
qualification claim. Only the bit 7 error is read from the status; other R1 error bits are not. Linux allows 30 s for a
flush, while `CONFIG_SD_DATA_TIMEOUT` defaults to 10 s; a composition that needs longer raises that value. The timeouts are
counted in polling delays, as `sdmmc_wait_ready` does, so they are a lower bound. The disk de-initialisation path also
sends the sync and discards its result. The drift check covers `mmc.c` only. The correction also relies on bit 15 of
`card->flags` staying unused in `sd_spec.h`, on `sd_init_io` clearing the flags, and on `sdmmc_wait_ready` behaving as at the pin, so a Zephyr update that leaves `mmc.c` unchanged still needs this reviewed. CMD13 is sent without retries as a named decision: a transient status error makes the sync fail, and the caller gets that error. With the Reliance Edge file system, as designed, the volume then becomes read-only until a remount, which is the safe direction; that adapter does not exist yet.

`scripts/prepare-mmc-cache-flush.py` checks the exact original source hash, applies the patch in a temporary directory and
verifies the generated hash. `zephyr/mmc_cache_flush.cmake` replaces exactly one `subsys/sd/mmc.c` in the existing
`subsys__sd` library with this build-local copy, adds `subsys/sd` to that library's private include path, and builds the
copy with `-Wall -Wextra -Werror`. It never edits the Zephyr checkout. The generated `.json` records original, patch and
output hashes. Source drift or a missing or ambiguous library member fails configuration.

`tests/native/emmc_stack` runs the corrected stack against the simulator in the `crux.emmc.stack.flush.cached` and
`.flush.nocache` scenarios, and `tests/native/emmc_restart` checks that a sync through it outlives a process restart with
the cache on. `tests/native/emmc_stack/test_prepare.py` checks the drift and output-path refusals. The simulator is a model
written from the stack's needs, so these results are not device evidence. Target compilation and physical evidence are
separate gates.

The upstream change is the intended end state and needs its own approval. The patch is removed when the correction is
available at the pinned Zephyr.

# TCP initial sequence number that fails closed

`zephyr-isn-fail-closed.patch` changes one file, `subsys/net/ip/tcp.c`, of Zephyr `dccb09599635bdff17633fa7e9dab014b91dce90`
(SHA-256 `393b940e…`, and `94642fe2…` after the patch). The file is Intel Corporation copyright, Apache-2.0
licensed, and its notices remain. Owner decision D-c of 9 November 2026, row C11 of the RT1060-EVKB bench image:
[`docs/rt1060-isn-fail-closed.md`](../../docs/rt1060-isn-fail-closed.md).

At the pin, `tcp_init_isn()` (RFC 6528, `CONFIG_NET_TCP_ISN_RFC6528`, Zephyr's default) has two defects:

- It ignores the result of `sys_csrand_get()` for its 16-byte key and marks the key as read, so after one failed read the key stays all zero for the rest of the boot.
- It hashes with SHA-256 into a 16-byte buffer. PSA refuses that with `PSA_ERROR_BUFFER_TOO_SMALL`, which is ignored too, so the hash is never produced (wolfPSA leaves the buffer as it was, Mbed TLS fills it with `'!'`) and the key is never used.

With the patch, a missing key (the read failed) or a failed hash makes `tcp_init_isn()` return an error. A SYN to a listening port then gets a RST
(a refused connection, as a failed bind does), and `net_context_connect()` returns the error and sends nothing. The key is read again at the next
attempt. When the read works, the number is the hash of the key and the tuple plus the uptime, as the code intends.

`scripts/prepare-isn-fail-closed.py` applies it to a **build-local copy** of the pinned tree (it refuses a Git checkout), after
`scripts/prepare-wolf-tls-zephyr.sh` has made the copy and applied the TLS socket patch, and checks the hash of the original, the patch and the result.
It extends the one marker of the copy, `.crux-wolf-tls`, from `<pin> <tls patch sha256>` to `<pin> <tls patch sha256> <isn patch sha256>`, and writes no marker of its own (on a bare copy without the TLS marker it writes none). The bench guard compares that marker with the pin and the declared patches' hashes in order, and checks every patched file's hash (`patched_zephyr`, schema 2, in `bench_declaration.json`). The patch is the intended end
state only until Zephyr fixes the two defects upstream; the upstream status was not checked.
