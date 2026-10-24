# ROM-preserving storage execution candidate

Status: experimental software candidate, stacked on the maintenance storage evidence. The user approved a 100 MHz Boot FCB candidate and reuse of the ROM command table as software implementation inputs on 24 October 2026. This does not adopt a production boot profile, driver, provider or layout. **Do not program or boot the image.** Physical operations, merge and Issue closure remain unapproved.

## Selected design

Retain one maintenance RAM island before controller/watchdog startup. Preserve the ROM-established Flash mode and commands rather than invoking the normal Zephyr probe, LUT replacement or reclocking path. Refuse any unsupported observed state. Only one aligned 4 KiB erase sector is eligible for the experimental dispatcher; block and chip erase are excluded. Automatic NOR/system reset is forbidden. A controller buffer refresh is distinct from a NOR reset and is eligible only after successful NOR-ready and controller-idle confirmation.

The limited maintenance profile does not establish a permanent platform restriction or qualify M7 concurrent storage/controller/network workloads. Existing portable File and Configuration contracts are unchanged.

## Boot and read-confirm evidence

The sample stages a fresh copy of the exact pinned EVKB-B FCB. Its 512-byte final ELF/BIN block differs from a separately compiled original only at offset `0x46`: clock enum `7` (120 MHz) becomes `6` (100 MHz). The remaining 511 bytes, including the entire LUT, match. This is a block comparison, not a claim that the whole image is unchanged.

Before copying/entering the temporary RAM context, the actual preflight consumes a read-only predicate over raw clock, controller, geometry and selected LUT registers. The supported subset requires A1-only 8 MiB geometry and the existing READ0/status1/WREN3/sector5/page9 sequences including STOP tails. Clock derivation reuses the pinned NXP getter after excluding unsupported PLL/PFD inputs and zero divisors, then rejects changed clock observations and nominal rates above 104 MHz. This is a register-derived nominal rate, not physical SCK measurement. No clock, LUT or mode writes or Flash commands are needed for this preflight.

Pinned source inputs are Zephyr `684c9e8f32e4373a21098559f748f06915f950c9` and hal_nxp `2c2f28ac333e995d7279777409817c4b4f92c1ec`. Source fingerprint checks refuse drift. Upstream checkouts and the earlier accepted PoC generator remain unchanged.

## Private command experiment

The isolated NOR translation unit reuses the pinned status, WREN, sector-erase and page-program helper bodies, with explicit mappings to ROM slots 1, 3, 5 and 9. Private IP reads use ROM slot 0. The generic native driver remains refused; its other compiled helpers are not an allowed dispatcher surface. A sector request must be exactly 4 KiB and aligned; a page request is at most 256 bytes and cannot cross a page or declared data owner. Firmware/header/out-of-owner requests are refused.

Before the busy context, preparation copies program data into private aligned RAM storage and binds only the MEMC fields actually consumed by port A1 transfers. It does not invoke normal probe, device configuration, reclocking or LUT installation. The typed dispatcher runs only after the temporary RAM stacks/vectors and interrupt guard are established. Initial NOR-ready/controller-idle confirmation precedes mutation or data read; post-command ready is a separate check. Errors or uncertain waits enter the internal terminal before restoration. Controller buffer refresh is restricted to positively ready/idle completion of a mutation.

The elapsed-time leaf deliberately fails closed on the target, and submission always refuses. Controlled tests substitute deterministic elapsed observations to exercise the otherwise inadmissible lower implementation. This is not a timer implementation, target-enabling flag or adopted driver. JEDEC 9Fh has no inherited ROM slot and is not installed; identity/protection remains unqualified. Result bytes stay in private RAM; this experiment is not the File/Configuration target binding.

The actual preflight now verifies every relocated ITCM text and DTCM read-only byte against its load image. A PRE_KERNEL_1 startup check verifies initialized DATA and zero BSS once, after Zephyr relocation and before the guarded POST_KERNEL drivers. Later legitimate command/buffer mutations are preserved, not overwritten to pass a check. Invalid extents, mismatches and unsupported sequencing latch refusal. This is implemented software verification with simulated and ELF evidence; physical execution of the verifier is untested. It does not authenticate NOR contents or detect arbitrary later mutable-memory corruption.

## Remaining approval and safety gates

No target elapsed-time source is selected: GPT2 and DWT are not adopted. Observation budgets are not elapsed deadlines. Identity, NOR status/protection, initial watchdog and external supervisor state, other masters, bus/cache/exception execution and exact target image/region/backup/recovery evidence must be established before physical admission. A controlled software fixture is not hardware qualification.

The existing maintenance lifecycle, independent LittleFS/scratch/NVS ownership, fail-closed uncertainty and explicit provisioning requirements remain in [the maintenance profile](storage-maintenance-profile.md). No automatic formatting or unqualified destructive recovery is allowed. Exact software command-path results and remaining dependencies are recorded separately from the historical read-only checkpoint.
