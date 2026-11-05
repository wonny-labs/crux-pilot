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
between those tags. It has been compiled and has not been run. How a composition applies it without editing the Zephyr
checkout is decided under Issue #185. The wolfSSL, wolfSSH and wolfPSA projects are fetched only when a workspace enables the
`crux-secure-management` group. wolfSSL and wolfSSH are offered under GPL-3.0-or-later or a commercial licence. wolfPSA's source
states GPL-3.0-or-later only. A vendor article says it has the same licence options as wolfSSL, and its commercial terms need
confirming with the vendor. Binaries built with these stacks are subject to those terms when distributed. The `mbedtls` and `tf-psa-crypto` projects in the same group are
Apache-2.0 OR GPL-2.0-or-later.

# eMMC cache flush on disk sync

`mmc-cache-flush.patch` is an opt-in correction of the Zephyr MMC stack. It applies only to `subsys/sd/mmc.c` at Zephyr
`dccb09599635bdff17633fa7e9dab014b91dce90`. The original file is NXP copyright, Apache-2.0 licensed; its notice remains in
the generated copy.

At the pin, `mmc_card_init` turns on the eMMC cache whenever the device reports one, and `DISK_IOCTL_CTRL_SYNC` only waits
for the card to be ready. No flush command exists in the stack, so on a device with a cache a successful sync does not mean
that the data reached the medium. The eMMC simulator in `tests/support/emmc_sim` reproduces this; it has not been observed
on a device.

With the correction, `mmc_set_cache` records in a private bit of `card->flags` that it turned the cache on. `sd_init`
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
sends the sync and discards its result.

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
