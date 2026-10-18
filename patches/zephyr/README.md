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
