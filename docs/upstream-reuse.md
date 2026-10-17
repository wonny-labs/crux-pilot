# Upstream reuse and M0 source assessment

Initial assessment date: 2026-10-16. Selected source baseline: Zephyr v4.4.0, peeled commit `684c9e8f32e4373a21098559f748f06915f950c9`. The example-application reference is v4.4.0, peeled commit `aaff110f0d5656e8346ca43eddd1c874bf8a2f93`. Implementation results are recorded in [the evidence log](baseline.md) and [AC tracker](implementation-status.md).

No upstream source files were copied into Crux. Official examples are used as path/build references. The west manifest pins Zephyr by commit and imports only `cmsis` and `hal_nxp` from that manifest. Zephyr/SDK sources stay as parent-workspace siblings, not under this Git repository.

| Upstream source | Observed behavior / role | Crux decision |
| --- | --- | --- |
| [`samples/drivers/rtc`](https://github.com/zephyrproject-rtos/zephyr/tree/v4.4.0/samples/drivers/rtc) | `src/main.c` checks readiness, writes a fixed 2024 date at boot, then reads each second. `sample.yaml` expects that fixed output and does not list `mimxrt1060_evk`. | Reference device/API usage only. Do not reuse fixed startup write or claim EVK support from its test metadata. |
| [`samples/subsys/shell/shell_module`](https://github.com/zephyrproject-rtos/zephyr/tree/v4.4.0/samples/subsys/shell/shell_module) | `README.rst` documents static and conditional commands, dynamic commands, dictionary commands, bypass callbacks and login/obscured input. `CMakeLists.txt` adds the shell sample sources; `sample.yaml` exercises native_sim with the UART PTY backend. | Reuse static command registration and Shell/UART transport. The Crux adapter delegates command semantics to its portable handler; Ztest uses Zephyr's dummy Shell backend to verify that route. Dynamic command management, login, bypass and unrelated commands are not included. |
| [`samples/cpp/hello_world`](https://github.com/zephyrproject-rtos/zephyr/tree/v4.4.0/samples/cpp/hello_world) | Uses `CONFIG_CPP=y` and `CONFIG_REQUIRES_FULL_LIBCPP=y`; prints the configured board from C++. | M1 build-only C++ runtime smoke input. No broader runtime setup inferred from this sample. |
| [`boards/nxp/mimxrt1060_evk/mimxrt1060_evk.dtsi`](https://github.com/zephyrproject-rtos/zephyr/blob/v4.4.0/boards/nxp/mimxrt1060_evk/mimxrt1060_evk.dtsi) and [`dts/arm/nxp/imxrt/nxp_rt10xx.dtsi`](https://github.com/zephyrproject-rtos/zephyr/blob/v4.4.0/dts/arm/nxp/imxrt/nxp_rt10xx.dtsi) | Board compatible is `nxp,mimxrt1062`; its `rtc` alias points to an enabled `zephyr,rtc-counter` child under `nxp,imx-snvs-rtc`. | The `mimxrt1060_evk@B//qspi` firmware cross-build passes. This proves board configuration and linkage only; actual board revision, flash state and SNVS behavior still require physical validation. |
| [`include/zephyr/drivers/rtc.h`](https://github.com/zephyrproject-rtos/zephyr/blob/v4.4.0/include/zephyr/drivers/rtc.h) | `struct rtc_time` calendar members are `int`, `tm_year` is years since 1900. | Calendar field width itself accommodates 2000–2099; this does not prove driver/libc conversion behavior. |
| [`drivers/counter/counter_mcux_snvs.c`](https://github.com/zephyrproject-rtos/zephyr/blob/v4.4.0/drivers/counter/counter_mcux_snvs.c) and [`drivers/rtc/rtc_counter.c`](https://github.com/zephyrproject-rtos/zephyr/blob/v4.4.0/drivers/rtc/rtc_counter.c) | MCUX SNVS counter matches `nxp,imx-snvs-rtc`, reports 1 Hz and `UINT32_MAX` top. Generic RTC Counter uses 64-bit calendar-to-ticks input and an `int64_t` epoch offset, but converts output through `time_t`/`gmtime_r` and rejects seconds above `UINT32_MAX`. | Calendar seconds in 2000–2099 fit the 32-bit unsigned counter limit and the expected SNVS 1 Hz range. Target `time_t`/libc conversion must still be built and tested; source arithmetic does not prove 2099 support. |
| [`example-application v4.4.0`](https://github.com/zephyrproject-rtos/example-application/tree/v4.4.0) | Reference shows an out-of-tree Zephyr module's `module.yml` and a distinct Zephyr module CMake entry point. | Crux metadata points to `zephyr/module.yml` entries in `zephyr/`; root `CMakeLists.txt` remains independent host CMake. Explicit `ZEPHYR_EXTRA_MODULES` is used by the M1 smoke command because the manifest repo is the workspace's self project. |
| [`tests/kernel/mem_slab/mslab_concept`](https://github.com/zephyrproject-rtos/zephyr/tree/v4.4.0/tests/kernel/mem_slab/mslab_concept) | Small upstream Ztest/Twister suite candidate. | Selected for the M1 `native_sim/native/64` smoke execution; the run passed. Current Crux RTC/Shell Ztest evidence is tracked separately. |

Pinned identity commands and results:

```text
git ls-remote https://github.com/zephyrproject-rtos/zephyr.git refs/tags/v4.4.0 'refs/tags/v4.4.0^{}'
4f50f0ba8905f27b2f60123d0ee0934fda6fe134 refs/tags/v4.4.0
684c9e8f32e4373a21098559f748f06915f950c9 refs/tags/v4.4.0^{}

git ls-remote https://github.com/zephyrproject-rtos/example-application.git refs/tags/v4.4.0 'refs/tags/v4.4.0^{}'
556894985c0c62529bb2f55be3a97a3061244650 refs/tags/v4.4.0
aaff110f0d5656e8346ca43eddd1c874bf8a2f93 refs/tags/v4.4.0^{}

git ls-remote https://github.com/google/googletest.git refs/tags/v1.15.2
b514bdc898e2951020cbdca1304b75f5950d1f59 refs/tags/v1.15.2
```

Official source reads confirmed the RTC startup behavior, sample metadata, C++ configuration, board alias, generic counter conversions, and module-entry pattern. RT1060 EVK remains absent from the RTC sample's allow-list; this is absence of sample coverage, not proof of board incompatibility.
