# PR #41 terminal-timeout correction

Parent snapshot: `8ef39d8dc218fbd0b906658826eea2669b2f1a8e`. The correction
commit contains this source snapshot; `source-sha256.txt` identifies its 66
source/contract/test/build inputs before commit. Original evidence remains
historical. Final-head hosted results are recorded in the PR, separately from
these pre-commit local logs. Machine roots and trailing whitespace normalized.

## Reproduction and outcomes

- Before production correction: Host `ctest --preset host-debug -R Queue.Expired`
  failed both expired receive/send on the expected TIMEOUT status assertion.
  Actual Zephyr Queue Twister had six passes and two failures on that same
  assertion. `host-red.txt` and `zephyr-red.txt` preserve these results.
- After correction: the two focused Host cases passed; standard
  `scripts/check-host.sh` and `CRUX_HOST_PRESET=host-sanitize scripts/check-host.sh`
  each passed 88/88 plus sample (`host-debug.txt`, `host-sanitize.txt`).
- `scripts/test-messaging.sh` exited0: independent Host consumers2/2;
  native Queue8 + Monotonic3 + runtime1 =12/12, then combined EVKB cross-build.
- `scripts/measure-messaging.sh` exited0 with six API retention checks and
  same-main/config paired static measurements. Flash32,088 ->34,976 (+2,888);
  allocated RAM7,360 ->11,648 (+4,288). Combined runtime cross-build:
  Flash39,488/RAM29,696. No physical measurement.
- Format/dependency gates and clang-tidy for the changed production code plus
  new Host fixture exited0. Initial extra test analysis rejected GNU reserved
  wrapper C identifiers; asm symbol aliases retained linker spelling while using
  ordinary C identifiers. No suppression was added. `analysis-initial.txt` is
  a tooling finding, not a behavioural RED test; `analysis.txt` is the correction.
  Full Host suites were rerun after this instrumentation-only edit.

Use the documented Linux container with `PATH` including the pinned tool venv,
`CLANG_FORMAT=clang-format`, external `CRUX_WORKSPACE`, SDK1.0.1 and Zephyr4.4.0.
For the native RED: `west twister -p native_sim/native/64 -T tests/queue/zephyr`
with this repository explicitly selected as ZEPHYR_MODULES and the absolute test
path when run from the external workspace. Canonical GREEN uses the tracked
script. Host caches were build/cache-debug and build/cache-sanitize.

## Instrumentation and assertions

Host wraps the real pthread timed wait. On actual ETIMEDOUT, libc already owns
metadata; this fixture deliberately releases it, exposes late availability,
then delays reacquisition. It models the common cleanup gap and does not assert
libc's internal reacquisition ordering. Zephyr wraps actual z_impl_k_condvar_wait
and pauses its -EAGAIN return before provider mutex reacquisition. Main holds
metadata while resuming each worker, so completion cannot precede release.

Both directions verify BUSY destroy with preserved handle during cleanup,
terminal TIMEOUT, unchanged receive destination, late FIFO records exactly once,
no ghost send, subsequent ordinary operations and safe destroy/reuse with stale
alias rejection. Production admitted-reference cleanup remains unchanged.
Successful wake/expiry races remain allowed; an already reported timeout cannot
be converted to success. No API, cancellation mechanism or scope was added.

Historical process-exit delays remain unresolved. Later passes neither identify
their cause nor resolve them. No hardware, PR merge or issue closure occurred.
