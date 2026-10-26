# M5 integrated software smoke evidence

## Scope

This fixture checks that the portable Thread, Sync, Logging and System components coexist in one Zephyr application. The native simulator creates and joins a worker, exercises a mutex and semaphore handoff (including an empty zero-time poll), submits a Logging record and verifies it reaches the emulated Logging UART, and reads System status without invoking reset or watchdog operations.

The EVKB Revision B target is cross-built only. This evidence does not include programming, physical UART traffic, reset, watchdog activation or on-board resource measurements.

## Reproduction

Run from the repository in the pinned M5 development container:

```sh
CRUX_WORKSPACE=/workspaces/crux \
ZEPHYR_BASE=/workspaces/crux/zephyr \
./scripts/test-m5-integrated-smoke.sh
```

The run used Zephyr `v4.4.0` and Zephyr SDK `1.0.1`, on 2026-10-26. Its source baseline was `03f32e63496edf38aee8a3b30a000cc500256a09`; the test and workflow changes were present as uncommitted candidate inputs. The provenance snapshot covered 1,082 files and had SHA-256 `f200a12705bcf0d8d76d96e12ffbccb4f7934e2427386c73ee5ec13f6a803b7f`. The runner compared the before/after source manifests successfully.

## Results

- `native_sim/native/64`: 1/1 test passed (`crux.m5.integrated_smoke`). The test observed a complete submitted Logging record on the emulated UART.
- `mimxrt1060_evk@B//qspi`: cross-build completed successfully.
- Static reports from that cross-build: ROM `62,404` bytes (`rom.json` root total), RAM `34,523` bytes (`ram.json` root total), and generated `zephyr.bin` `62,412` bytes. These are build outputs for this test configuration, not production capacity guarantees or measured board usage.
- EVKB configuration selected `CRUX_THREAD`, `CRUX_SYNC`, `CRUX_LOGGING` and `CRUX_SYSTEM`. Crux Serial was not enabled. The UART Logging backend targets LPUART1 at 115200 baud, 8N1. Zephyr stdout, console input/handler, console subsystem, early console and shell are disabled; `CONFIG_LOG_PRINTK=y` routes printk through Logging.
- The native simulator uses a separate UART emulator for Logging and keeps the Twister test console available with `CONFIG_EARLY_CONSOLE=y` and `CONFIG_LOG_PRINTK=n`.
- The workflow evidence collector includes the integrated Twister reports, source manifests, configuration, devicetree, map, provenance, resource reports, ELF and BIN outputs.

The cross-build emitted an upstream devicetree warning about the duplicate unit address `usbd@402e0000` / `usbh@402e0000`. No fixture overlay defines either node.

## Limits

This proves simulator behavior and compile-time composition only. It does not prove physical UART ownership, scheduling, stack margin, runtime resource use, reset behavior or EVKB compatibility. The ROM/RAM reports are static linker analyses for this test application; dynamic allocation and runtime stack high-water marks were not measured.
