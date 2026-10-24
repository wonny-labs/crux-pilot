# Portable Logging

The [approved design](superpowers/specs/2026-10-24-logging-design.md) defines
`<crux/logging.h>` and `crux::logging`. The component depends publicly on Base.

Submit completed text with `crux_log_write(level, tag, text)`; configure the
process/boot-local threshold with `crux_log_set_level(maximum_level)`. Defaults
to INFO. ERROR, WARN, INFO and DEBUG are record levels; OFF is a setter value.
Invalid setters preserve the old level. Each valid submission samples the level
once; later filter changes do not withdraw already submitted native records.

Tags contain 1–24 ASCII letters, digits, `_`, `-` or `.`. Text contains 0–256
bytes without CR or LF. Limits exclude the terminating NUL. Both strings must
be readable and unchanged until return. The caller may modify or free both strings after return. Zephyr packages
writable strings and can reference immutable image-resident strings. Percent characters are literal; applications own any
formatting they perform before submission. Invalid inputs are rejected even
when the record would be filtered. Invalid arguments return INVALID_ARGUMENT;
excess length returns OUT_OF_RANGE. There is no silent prefix truncation.

OK means a valid filtered call or a native submission attempt. It does not
acknowledge queue admission, completed output or delivery. The native Zephyr
queue may drop a new record after OK; its aggregate loss report remains enabled.
There is no public per-record loss counter, flush, drain or retry operation.

Calls are task-context only after startup. ISR/pre-kernel calls return
NOT_SUPPORTED. Known unavailable devices return NOT_READY; known Host output
failure returns IO, potentially after partial output. Host output is synchronous
and serialized and may block in native locking/I/O. Zephyr uses its existing
deferred logger worker and buffer without waiting for capacity. Neither provider
promises a whole-call worst-case latency. Non-overlapping calls in one task keep
submission order among surviving records. Concurrent tasks have no total order.

The Linux provider writes `LEVEL tag: text` plus newline to stderr. It locally
blocks and consumes only a newly generated SIGPIPE when a pipe reader has gone
away, preserving the caller's mask and any previously pending SIGPIPE. It does
not install a process-wide handler. Unrelated raw stream writers are outside
the qualified composition.

The initial Zephyr provider maps to native LOG_ERR/WRN/INF/DBG with an explicit
DEBUG source ceiling, visible severity, a native line boundary and the common Crux filter. It requires deferred native
processing, UART TEXT output, autostart, drop-new, no capacity wait, no runtime
native filter or extra backend. An explicit single enabled chosen log UART is
required. Native_sim uses a dedicated UART emulator separate from its harness
Console. The approved revision-B EVKB LPUART1 profile compiles UART_CONSOLE as the
pinned backend's prerequisite but disables direct Console stdout, input and
subsystem helpers, early Console and Shell; printk routes through native Logging.
Build checks reject other Console-sharing configurations. See validation evidence
before adopting a board composition. No physical verification is implied.
