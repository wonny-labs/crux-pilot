# Test organization

## Host: `time/`

GoogleTest/CTest exercises the C17 Time implementation, calendar conversion, and portable command
handler against the controlled host backend. The C++ tests include the public C headers and call
the C17 implementation, providing the C++ consumer coverage needed for this API. The test build
does not recompile production `.c` sources as C++ and does not maintain per-header fixtures or a
separate link-only executable.
CTest applies a 30-second limit to each discovered host test.

The root CMake option `BUILD_TESTING=OFF` skips GoogleTest and C++ configuration. Set
`CRUX_BUILD_HOST_SAMPLE=OFF` as well to configure and build only the portable and host library
targets.

## Zephyr: `zephyr_time/`

Twister builds and runs the C17 Ztest application on two `native_sim/native/64` configurations: one
with an RTC alias and one without it. Tests cover Zephyr error mapping, RTC backend behavior, the
shared command handler, and the actual registered Zephyr Shell adapter using Zephyr's dummy Shell
backend. The trailing-argument case verifies that Shell passes syntax errors to the shared Crux
handler for the documented `ERR INVALID_ARGUMENT` result.

Host tests own portable behavior. Ztest covers native integration and the Shell boundary. The two
suites do not duplicate every case across frameworks.

## Build tools: `build/`

Python standard-library `unittest` checks the provenance safeguards: changed input rejection,
actual dependency HEAD/dirty-state checks, and invalidation of stale manifests even when a debug
build fails. Run `python3 -m unittest discover -s tests/build -v` on Linux; host CI runs it too.
No additional test framework is required.
