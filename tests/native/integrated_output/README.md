This finite native_sim fixture uses actual production format literals, native
LOG_PRINTK with one UART backend, a 16 KiB drop-new buffer, and the existing
native logger resource helper. Main priority 0 publishes 10 Hz feed payloads and
1 Hz observer/stack/logger groups; worker priority 5 and network priority 6 publish
finite Time/GPIO and pair-burst payloads. The verifier requires every record in
per-producer order, allowing cross-producer interleaving. A separate finite
high-priority burst must expose native message loss. Six mutations of the actual
lossless handler reject whitespace, ANSI, duplication, truncation and sequence
corruption; a seventh rejects producer work moved across the explicit fixture
fault/boot boundary.

All functional, stack and fault/boot payload values except the logger resource
query are synthetic. This fixture does not access Time, GPIO, sockets or watchdog
hardware, and its boot payload does not represent a physical reset. Native logger
stack observations are checked against their native allocation, without applying
Cortex-M high-water claims to the host. This is output-path and bounded workload
stream evidence; the original integrated target oracles and physical runs remain
required. It does not establish WCET, physical service rate or M5 qualification.
