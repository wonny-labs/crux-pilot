# Monotonic capability tests

The standalone Host project builds C17 and C++17 consumers through the public
`crux::monotonic` facade. Final native composition selects `crux_monotonic_host`
separately. Its CMake checks the actual public usage graph and C++ consumer graph.

```sh
cmake -S tests/monotonic/host -B build/monotonic-host -G Ninja
cmake --build build/monotonic-host
ctest --test-dir build/monotonic-host --output-on-failure
```

- `Monotonic.Common` binds the real shared implementation to a private fake native
  port. It checks preserved outputs after failed/invalid reads, range limits,
  immediate past deadlines, repeated early wakes without renewing the deadline,
  failure propagation, ISR rejection and checked tick conversion boundaries.
- `Monotonic.NativeBoundaries` binds the real Host provider and uses linker-only
  native call wrappers. It checks malformed and overflowing `timespec` values,
  floor conversion, read failure, interrupted absolute waits, original deadlines
  and native sleep failure. Wrappers affect this executable only.
- `Monotonic.RealHost` uses the production native clock and sleep functions through
  a public C++17 consumer, checks nondecreasing observations and reaches a finite
  absolute deadline. Its five-second test timeout is a broad test-harness limit,
  not a return-latency guarantee.

For Zephyr, enable the Crux module and select this checkout explicitly where the
west manifest also lists another Crux checkout. In the documented Linux workspace:

```sh
export ZEPHYR_BASE=/workspaces/crux/zephyr
west build -b native_sim/native/64 -d build/monotonic-zephyr \
  tests/monotonic/zephyr -- -DZEPHYR_MODULES="$PWD" -DZEPHYR_EXTRA_MODULES=
build/monotonic-zephyr/zephyr/zephyr.exe
```

The native fixture runs at 128 ticks/second: 125 ms is exactly 16 ticks, while
126 ms requires 17 ticks. Assertions compare actual uptime ticks after the public
wait and check that observation reaches the deadline. A real interrupt-offload
probe checks `NOT_SUPPORTED` and preserves the read output. No hardware is used.
