# Reference-controller native MDIO correction

`nxp-enet-mdio-completion.patch` is an opt-in correction for the single-core
RT1062 reference composition. It applies only to
`drivers/ethernet/mdio/mdio_nxp_enet.c` at Zephyr
`684c9e8f32e4373a21098559f748f06915f950c9`. The original file is NXP copyright,
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
