# Device-control tool and consumer check runs — 4 November 2026

Evidence for steps 3 and 4 of the [test foundation proposal](../../test-foundation-proposal.md#recommended-order), [Issue #169](https://github.com/wonny-labs/crux-pilot/issues/169). **Software only. No hardware, flash, reset or real serial port was used, and nothing ran on Windows.** These runs do not qualify a physical board. The PoC runs are in the [earlier record](../test-foundation-poc/README.md), which stays as it was.

GitHub Actions results for the merged head are on [PR #171](https://github.com/wonny-labs/crux-pilot/pull/171), not here, because a record cannot name the commit that contains it.

## Revisions

- Tool code and its tests: `6958f582f80445542621d1a7edab879a649ae5e2`, which includes `origin/main` at `82d2c9b57575277415ca798256d00a57a09418ab`.
- Consumer check, `native_sim` and functional levels: fixture content of `7cf471092872b57edf8e748b2dc2c6de0b801848`, at Zephyr v4.4.0 (`684c9e8f32e4373a21098559f748f06915f950c9`), the `west.yml` pin at that time.
- `west.yml` now pins Zephyr v4.4.2 (`dccb09599635bdff17633fa7e9dab014b91dce90`). **The Zephyr levels did not run at this pin.** Upstream's commit history for `scripts/pylib/pytest-twister-harness`, `cmake/modules/zephyr_module.cmake`, `cmake/modules/west.cmake` and the four Zephyr files the harness imports has the same latest commit at both pins. That is history reading, not a run.

## Results

Run by the authoring agent, not by a reviewer.

| Check | Where | Revision | Result |
| --- | --- | --- | --- |
| `python3 -m unittest discover -s tests/build -p test_device_control.py` | macOS 15.7.4 arm64, Python 3.9.6, no pyserial | `6958f58` | 93 tests OK, 1 skipped (the pseudo-terminal test) |
| The same | The same, in a venv with pyserial 3.5 | `6958f58` | 93 tests OK |
| GitHub Actions `host-foundation`, `tests/build` discovery | ubuntu-24.04, no pyserial | `d56f35c`, same tool code as `6958f58` | 479 tests OK, 8 skipped. Of these, the pseudo-terminal test skipped (`pyserial is not installed`), the 5 consumer Host tests passed and the opt-in Zephyr case skipped |
| `test_device_control.py` with `test_m5_runner.py`, one process | Linux container, Python 3.12.3, with pyserial | `7cf4710` | 97 tests OK. This is the last run of real pyserial on Linux |
| `test_consumer_poc.py`, Host levels | Linux container | Fixture content of `7cf4710` | 5 passed, 1 skipped (the opt-in Zephyr case) |
| `test_consumer_poc.py` with `CRUX_CONSUMER_POC_ZEPHYR_TESTS=1` | Linux container, Dev Container layout, Zephyr v4.4.0 | Fixture content of `7cf4710`, before commit | Passed in 286 s, including the functional oracle rejecting the controlled defect |

The Linux container was image `crux-pilot-dev:latest` as linux/amd64, emulated on Apple Silicon. It was not rerun at `6958f58`: the local Docker daemon did not respond while other work held it, so Linux at that revision rests on GitHub Actions, without pyserial.

## Not run

- Windows, anything. One test runs the `msvcrt` branch against a fake module, which proves nothing about Windows.
- Hardware, anything: no real serial port, probe, reset, flash or `west flash`. One test uses real pyserial on a disposable pseudo-terminal, which has no DTR or RTS lines.
- The Zephyr levels at Zephyr v4.4.2, and the consumer workspace layout of `run_poc.sh` after the PoC record.

## Stopping a tool on macOS: a known defect, not fixed

A review reported from memory that `os.killpg` may raise `PermissionError` on macOS for a group that holds only a zombie. [`stop-probe.py`](stop-probe.py) confirms it, at the tool code of `6958f58`. It starts a session leader with `start_new_session=True`, waits one second without reaping it, and then signals the group. Its output is kept in [`stop-probe-macos.txt`](stop-probe-macos.txt):

| Case | Result on macOS 15.7.4, Python 3.9.6 |
| --- | --- |
| `os.killpg`, leader exited, not reaped | `PermissionError`, errno 1 |
| `os.killpg`, leader exited, not reaped, a child still running | No error |
| `os.killpg`, leader reaped | `ProcessLookupError`, errno 3 |
| `run._stop`, tool exited, not reaped | `PermissionError`, errno 1, with `returncode` still `None` |

What this means, from reading `run.py`:

- It needs a tool to exit between `process.wait(timeout)` giving up and the signal, or while an interrupt is being handled. That race was not reproduced through `run_tool` itself.
- In the case probed, nothing is left running. A group member this user may not signal, such as a tool run through `sudo`, could give the same error on any POSIX host while it keeps running. That was not probed, so a fix must not simply ignore this error.
- The operation fails, but with `PermissionError` instead of `ToolFailed`. `events.jsonl` then has no `tool_failed` event, and the command line prints a traceback rather than an error line.
- If an interrupt arrived while the tool ran, this error replaces it, because `run_tool` calls `_stop` before re-raising.
- The lock release and `result.json` still happen, through `BoardRun`'s normal cleanup.

Linux was not probed.

## Commit anchors

The [PoC record](../test-foundation-poc/README.md) names `ae7bdbc`, `b185e4e` and `0692fa0`. They are ancestors of PR #171's head, so `refs/pull/171/head` reaches them whichever merge method is used.
