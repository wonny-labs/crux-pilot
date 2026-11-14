# Portable controller timing scenario

`controller.c` is a separate bounded reference-controller scenario using only
`crux::time` and `crux::thread`. Its 100 ms phase and skip-overrun policy remain application
logic. Build and run it through `tests/messaging_consumer/CMakeLists.txt`.
Thread is frozen for new product code (see
[Choosing an API](../../docs/choosing-an-api.md)): the scenario stays as a
bounded reference for existing users and is not a pattern for new product code.
Historical controller/RTC fixtures are preserved. Host functional execution does
not establish production runtime compatibility, timing jitter or target stack
margins.
