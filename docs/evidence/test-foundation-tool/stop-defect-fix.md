# Stopping a device-control tool: fix and probes — 4 November 2026

Evidence for the fix to `_stop` in `tools/device_control/run.py`, [Issue #169](https://github.com/wonny-labs/crux-pilot/issues/169). **Software only. No hardware, flash, reset or real serial port was used, and nothing ran on Windows.** The [earlier run record](README.md) describes this defect as not fixed, and it stays as it was.

## Revisions

- Fix and tests: `ef54643a1882ba18d346d37d0e6f3839b7f8736c`, on top of `61924bb41570ad264190befd830123d452681422`.
- The "before" results use that same test file with `run.py` from `61924bb`.

## What the probes found

The defect is wider than the `PermissionError` first reported.

| Case | Before | After |
| --- | --- | --- |
| macOS: a tool exits just before it is stopped | `PermissionError` instead of `ToolFailed`, or in place of an interrupt | A plain timeout, or the interrupt |
| Linux, real `sudo` 1.9.15p5: `sudo` can be signalled and dies, its root child stays in the group | A plain timeout, **with no sign that the root child still runs**, and the lock released | The error says the tool may still be running. The lock is still released |
| Linux, tool stays root and this user may not signal any of the group | `PermissionError` from `_stop`, which also skips the wait | `_stop` returns `False` without waiting |
| Linux, root tool exited, not reaped, nothing else in the group | `PermissionError` | `_stop` returns `True` |

Raw output: [`stop-probe-unsignallable-linux.txt`](stop-probe-unsignallable-linux.txt), from [`stop-probe-unsignallable.py`](stop-probe-unsignallable.py), and [`stop-probe-macos-after-fix.txt`](stop-probe-macos-after-fix.txt), from [`stop-probe.py`](stop-probe.py). These show why the fix cannot simply ignore the error: the same `PermissionError` means nothing is left in one case and a running root process in another. So `_stop` reaps the tool and then checks whether the group has gone.

## Results

Run by the authoring agent, not by a reviewer, at the fix commit unless a row says otherwise.

| Check | Where | Result |
| --- | --- | --- |
| `test_device_control.py` | macOS 15.7.4 arm64, Python 3.9.6, no pyserial | 103 tests OK, 2 skipped: the pseudo-terminal test, and the real `sudo` test, because `sudo` here needs a password |
| `test_device_control.py` | Linux container, user `vscode`, Python 3.12.3, pyserial 3.5, `sudo` without a password | 103 tests OK, including both of those |
| `test_m5_runner.py` | The same container, no pyserial | OK |
| The real `sudo` test with `run.py` from `61924bb` | The same container | Fails: `False != True`, so the error gave no warning while a member stayed in the group |
| The new tests with `run.py` from `61924bb` | macOS | Fail with `PermissionError`, or on the return value |

The Linux container is image `crux-pilot-dev:latest` as linux/amd64, emulated on Apple Silicon, with the source mounted read-only.

`test_timeout_also_stops_what_the_tool_started` failed twice in that container while the host was busy, with `the child did not start before the timeout`. It failed the same way with `run.py` from `61924bb`, and it passed in the later run. Its 5 s timeout can be too short on a loaded emulated host. This fix does not change it.

GitHub Actions results are on the pull request.

## What is not shown

- **macOS with a real tool this user may not signal.** `sudo` here needs a password, which nothing here supplies. A `sudo` waiting at its prompt keeps this user's real user ID and can be signalled, so it is no stand-in. macOS covers this path only with a faked `os.killpg`.
- **Which path `sudo` takes on GitHub's Linux runner.** The test accepts either: a member stays in the group and the error warns, or the group has gone and it does not. A `sudo` configured to run its command in its own session leaves the group, and this check cannot see it.
- **A leader that survives a signal that worked.** `_stop` still waits for it without a limit, so a tool in uninterruptible sleep can hold the run.
- **A member that left the group.** It is not seen, as before.
- **The lock.** A tool that could not be stopped no longer holds the board's lock, because the run ends. The error says so; nothing prevents the next run.
- Windows and hardware, anything.
