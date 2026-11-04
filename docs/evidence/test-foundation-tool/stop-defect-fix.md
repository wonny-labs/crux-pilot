# Stopping a device-control tool: fix and probes — 4 November 2026

Evidence for the fix to `_stop` in `tools/device_control/run.py`, [Issue #169](https://github.com/wonny-labs/crux-pilot/issues/169). **Software only. No hardware, flash, reset or real serial port was used, and nothing ran on Windows.** The [earlier run record](README.md) describes this defect as not fixed, and it stays as it was.

## Revisions

- Fix: `ef54643a1882ba18d346d37d0e6f3839b7f8736c`, on top of `61924bb41570ad264190befd830123d452681422`.
- Tests and probe script as run: `bbc98310ef48f5e179156deb709e62ac01beec04`. `run.py` is the same as at the fix.
- The "before" results use those tests with `run.py` from `61924bb`.

## What the probes found

The defect is wider than the `PermissionError` first reported.

| Case | Before | After |
| --- | --- | --- |
| macOS: a tool exits just before it is stopped | `PermissionError` instead of `ToolFailed`, or in place of an interrupt | A plain timeout, or the interrupt |
| Linux, real `sudo` 1.9.15p5: `sudo` can be signalled and dies, its root child stays in the group | A plain timeout, **with no sign that the root child still runs**, and the lock released | The error says the tool may still be running. The lock is still released |
| Linux, tool stays root and this user may not signal any of the group | `PermissionError` from `_stop`, which also skips the wait | `_stop` returns `False` without waiting |
| Linux, root tool exited, not reaped, nothing else in the group | `PermissionError` | `_stop` returns `True` |
| Linux, root leader with a member this user may signal | `_stop` waits without a limit | **The same. Not fixed:** the signal reaches only the member, and the probe was still waiting after 5 s |
| Linux, a killed child in a container whose PID 1 does not reap orphans | A plain timeout | A false warning that the tool may still be running, and 1 s more, because a zombie stays in the group |

The "before" cells for the third and fourth rows rest on the `killpg` lines in the raw output and on one `_stop` run against a live root tool at `61924bb`, whose output was not kept.

Raw output: [`stop-probe-unsignallable-linux.txt`](stop-probe-unsignallable-linux.txt), from [`stop-probe-unsignallable.py`](stop-probe-unsignallable.py), and [`stop-probe-macos-after-fix.txt`](stop-probe-macos-after-fix.txt), from [`stop-probe.py`](stop-probe.py). These show why the fix cannot simply ignore the error: the same `PermissionError` means nothing is left in one case and a running root process in another. So `_stop` reaps the tool and then checks whether the group has gone.

## Results

Run by the authoring agent, not by a reviewer, at the fix commit unless a row says otherwise.

| Check | Where | Result |
| --- | --- | --- |
| `test_device_control.py` | macOS 15.7.4 arm64, Python 3.9.6, no pyserial | 103 tests OK, 2 skipped: the pseudo-terminal test, and the real `sudo` test, because `sudo` here needs a password |
| `test_device_control.py` | Linux container with an init, user `vscode`, Python 3.12.3, pyserial 3.5, `sudo` without a password | 103 tests OK, including both of those |
| `test_m5_runner.py` | The same container, no pyserial | OK |
| `test_a_wrapper_and_what_it_started_count_as_stopped` | The same image without an init, where PID 1 does not reap | Skipped, with that reason |
| The real `sudo` test with `run.py` from `61924bb` | The same container | Fails: `False != True`, so the error gave no warning while a member stayed in the group |
| The new tests with `run.py` from `61924bb` | macOS | Fail with `PermissionError`, or on the return value |

The Linux container is image `crux-pilot-dev:latest` as linux/amd64, emulated on Apple Silicon, with the source mounted read-only. Commands:

```sh
python3 -m unittest discover -s tests/build -p test_device_control.py
docker run --rm --init --platform linux/amd64 --cpus 2 --memory 2g --user vscode \
  -e PYTHONDONTWRITEBYTECODE=1 -v "$PWD":/work:ro -w /work crux-pilot-dev:latest bash -c \
  'python3 -m pip install -q --target /tmp/pyserial pyserial==3.5 &&
   PYTHONPATH=/tmp/pyserial python3 -m unittest discover -s tests/build -p test_device_control.py'
docker run --rm --platform linux/amd64 --cpus 1 --memory 1g --user 0 -e PYTHONDONTWRITEBYTECODE=1 \
  -v "$PWD":/work:ro crux-pilot-dev:latest python3 -u -I \
  /work/docs/evidence/test-foundation-tool/stop-probe-unsignallable.py /work/tools
python3 -I docs/evidence/test-foundation-tool/stop-probe.py "$PWD/tools"
```

The "before" runs add `-v <copy>:/work/tools:ro`, where the copy of `tools/` has `run.py` from `61924bb`. The macOS race is forced in the tests by patching `Popen.wait` to give up after the tool has exited; it was not caught happening by chance.

`test_timeout_also_stops_what_the_tool_started` failed in that container while the host was busy, with `the child did not start before the timeout`. It failed the same way with `run.py` from `61924bb`, and it passed in the later run. Its 5 s timeout can be too short on a loaded emulated host. This fix does not change it.

GitHub Actions results are on the pull request.

## What is not shown

- **macOS with a real tool this user may not signal.** `sudo` here needs a password, which nothing here supplies. A `sudo` waiting at its prompt is no stand-in: `ps` showed this user's real user ID with effective user root, `killpg` succeeded and it exited with -9. No password was entered, and nothing ran as root. That output was not kept. macOS covers this path only with a faked `os.killpg`.
- **Which path `sudo` takes on GitHub's Linux runner.** The test accepts either: a member stays in the group and the error warns, or the group has gone and it does not. A `sudo` configured to run its command in its own session would leave the group, and this check could not see that.
- **A leader that survives a signal that worked.** `_stop` still waits for it without a limit. The root leader above does this, and a tool in uninterruptible sleep could too. So a timeout does not bound the run in those cases.
- **A member that left the group.** It is not seen, as before.
- **The lock.** The run ends and releases the lock even if the tool could not be stopped, as it did before this fix. The error warns about the tool and does not mention the lock, and after an interrupt only the tool's log warns. Nothing prevents the next run.
- Windows and hardware, anything.
