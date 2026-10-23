# Portable controller timing scenario

`controller.c` is a separate bounded reference-controller scenario using only
`crux::monotonic`. Its 100 ms phase and skip-overrun policy remain application
logic. Build and run it through `tests/messaging_consumer/CMakeLists.txt`.
Historical controller/RTC fixtures are preserved. Host functional execution does
not establish production runtime compatibility, timing jitter or target stack
margins.
