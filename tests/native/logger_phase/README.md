# Native logger count and initial observation phase

This fixture uses the pinned Zephyr kernel, actual deferred `printk` path,
production format literals and `m3_logger_resources_get`, with the native UART
backend still enabled. An additional **test-only** backend renders the same
message to a byte counter and delays completion by the resulting 115200 baud,
10-bit-per-byte serial budget. The model uses the EVKB configuration's 10000 Hz tick resolution, avoiding the native default 100 Hz
rounding of every serial delay. It does not replace or modify production logging.

First, a semaphore holds one actual backend invocation for at most 100 ms. The
main thread observes one buffered message while processing is in flight, then
zero after release/completion. Setup and teardown drain loops are bounded at two
seconds; they never select an acceptance sample.

The finite workload has main/worker/network/logger priorities 0/5/6/14. Worker
reports recur every 1000 ms; the independent network idle reports recur every
1001 ms with a 1 ms initial offset. Main takes four fixed samples, with offsets
0/1/2/4 ms modelling the previously observed relative-loop drift. The corrected
case adds the production source's fixed 50 ms initial offset; the control adds
zero. Each sample enqueues the real observer/stack/logger formats afterwards.
The last two reports add twelve explicitly synthetic analyser lines, exercising
recurring post-report volume. These synthetic resource/health payloads are not
physical controller or watchdog evidence. Native logger identity/count/stack
observations themselves come from the kernel, but Cortex stack criteria do not
apply to this native mechanism fixture.

The verifier checks every expected production-format record and per-owner
sequence, raw grammar, native configuration, held-message mechanism, fixed
sample times and original first-three initial queue policy. The corrected case
must pass; the unshifted case must exhibit an in-flight initial count and fail
that same policy. It retains the actual pre-report byte budgets and observed
sample stamps. Mutations of the actual capture must reject nonzero first samples
even with later zeros, missing/duplicate/reordered/partial/ANSI records, late
samples and loss markers. No polling for queue zero or favourable resampling is
used in the qualifying sample path.

A one-time offset is not a phase lock. Relative loop/report cost and independent
network cadence can drift. This bounded model supports a physical hypothesis;
it proves neither Cortex UART latency nor continuous zero queue, worst-case
execution time, stack sufficiency or platform scheduling guarantees. Every new
physical capture must retain all logger observations and the unchanged initial
idle criteria. Armed reports may enqueue a watchdog-feed record before sampling;
zero queue is required at the initial idle checkpoint only.

The existing `scripts/test-m3.sh` runs both cases and their verifier. For a
focused check, run Twister on this directory for `native_sim/native/64`, then
run `verify_output.py` on that Twister output directory in the Linux development
environment.
