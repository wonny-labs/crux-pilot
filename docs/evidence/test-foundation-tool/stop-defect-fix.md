# Stopping a device-control tool: fix and probes — 4 November 2026

Evidence for the fix to `_stop` in `tools/device_control/run.py`, [Issue #169](https://github.com/wonny-labs/crux-pilot/issues/169). **Software only. No hardware, flash, reset or real serial port was used, and nothing ran on Windows.** The [earlier run record](README.md) describes this defect as not fixed, and it stays as it was.

## Revisions

- Fix: `ef54643a1882ba18d346d37d0e6f3839b7f8736c`, on top of `61924bb41570ad264190befd830123d452681422`.
- Limit on the wait, and its tests: `2cd336e5cc018ac3f268d31e92f50c6b6f2d8316`.
- Tests as run: `54971e670466f9ecd5ca031e0815e7f7ceae5285`. `run.py` is the same as at the limit.
- [`stop-probe-unsignallable-linux.txt`](stop-probe-unsignallable-linux.txt) is from `bbc98310ef48f5e179156deb709e62ac01beec04`, whose `run.py` is the same as at the fix, so it predates the limit. [`stop-probe-unsignallable-linux-with-limit.txt`](stop-probe-unsignallable-linux-with-limit.txt) is from `54971e6`. [`stop-probe-macos-after-fix.txt`](stop-probe-macos-after-fix.txt) is from `ef54643`.

## What the probes found

The defect is wider than the `PermissionError` first reported.

| Case | Before | After |
| --- | --- | --- |
| macOS: a tool exits just before it is stopped | `PermissionError` instead of `ToolFailed`, or in place of an interrupt | A plain timeout, or the interrupt |
| Linux, real `sudo` 1.9.15p5: `sudo` can be signalled and dies, its root child stays in the group | A plain timeout, **with no sign that the root child still runs**, and the lock released | The error says the tool may still be running. The lock is still released |
| Linux, tool stays root and this user may not signal any of the group | `PermissionError` from `_stop`, which also skips the wait | `_stop` returns `False` without waiting |
| Linux, root tool exited, not reaped, nothing else in the group | `PermissionError` | `_stop` returns `True` |
| Linux, root leader with a member this user may signal | `_stop` waits without a limit: the signal reaches only the member, and the probe was still waiting after 5 s | `_stop` gives up after 1 s and returns `False`, against a real root leader in the probe |
| Linux, a killed child in a container whose PID 1 does not reap orphans | A plain timeout | A false warning that the tool may still be running, and 1 s more, because a zombie stays in the group |

The "before" cells for the third and fourth rows rest on the `killpg` lines in the raw output and on one `_stop` run against a live root tool at `61924bb`, whose output was not kept.

The raw outputs listed under [Revisions](#revisions) come from [`stop-probe-unsignallable.py`](stop-probe-unsignallable.py) and [`stop-probe.py`](stop-probe.py). They show why the fix cannot simply ignore the error: the same `PermissionError` means nothing is left in one case and a running root process in another. So `_stop` reaps the tool and then checks whether the group has gone.

## Results

Run by the authoring agent, not by a reviewer.

| Check | Where | Result |
| --- | --- | --- |
| `test_device_control.py`, at `54971e6` | macOS 15.7.4 arm64, Python 3.9.6, no pyserial | 105 tests OK, 2 skipped: the pseudo-terminal test, and the real `sudo` test, because `sudo` here needs a password |
| The two limit tests with `run.py` from `cddbf13` | The same | Fail: the stop waited about 21 s, until the 20 s tool ended |
| The new stop tests with `run.py` from `61924bb` | The same | Fail with `PermissionError`, or on the return value |
| `test_device_control.py`, at `54971e6` | Linux container with an init, user `vscode`, Python 3.12.3, pyserial 3.5, `sudo` 1.9.15p5 without a password | 105 tests OK, including the pseudo-terminal and real `sudo` tests |
| `test_m5_runner.py`, at `54971e6` | The same container, no pyserial | OK |
| `test_a_wrapper_and_what_it_started_count_as_stopped`, at `54971e6` | The same image without an init, where PID 1 does not reap | Skipped, with that reason |
| The real `sudo` test, at `bbc9831`, with `run.py` from `61924bb` | The Linux container with an init | Fails: `False != True`, so the error gave no warning while a member stayed in the group |
| GitHub Actions `host-foundation`, `tests/build` discovery, at `fc9768a`, same `run.py` | ubuntu-24.04, no pyserial | 613 tests OK, 44 skipped. In the log, the real `sudo` test and both limit tests ran and passed |

The Linux container is image `crux-pilot-dev:latest` as linux/amd64, emulated on Apple Silicon, with the source mounted read-only. Commands:

```sh
# macOS
python3 -m unittest discover -s tests/build -p test_device_control.py
python3 -I docs/evidence/test-foundation-tool/stop-probe.py "$PWD/tools"

# Linux, with an init
docker run --rm --init --platform linux/amd64 --cpus 2 --memory 2g --user vscode \
  -e PYTHONDONTWRITEBYTECODE=1 -v "$PWD":/work:ro -w /work crux-pilot-dev:latest bash -c \
  'python3 -m pip install -q --target /tmp/pyserial pyserial==3.5 &&
   PYTHONPATH=/tmp/pyserial python3 -m unittest discover -s tests/build -p test_device_control.py &&
   python3 -m unittest discover -s tests/build -p test_m5_runner.py'

# Linux, without an init
docker run --rm --platform linux/amd64 --cpus 1 --memory 1g --user vscode \
  -e PYTHONDONTWRITEBYTECODE=1 -v "$PWD":/work:ro -w /work crux-pilot-dev:latest timeout 100 \
  python3 -m unittest discover -s tests/build -p test_device_control.py -k test_a_wrapper_and_what -v

# Linux, the probe, as root
docker run --rm --platform linux/amd64 --cpus 1 --memory 1g --user 0 -e PYTHONDONTWRITEBYTECODE=1 \
  -v "$PWD":/work:ro crux-pilot-dev:latest timeout 150 python3 -u -I \
  /work/docs/evidence/test-foundation-tool/stop-probe-unsignallable.py /work/tools
```

The macOS "fail" rows are the first command, with the new tests in the working tree and `run.py` not yet changed. The Linux "fails" row is the second `docker run` shape, adding `-v <copy>:/work/tools:ro`, where the copy of `tools/` has `run.py` from `61924bb`, and running only that test. The macOS race is forced in the tests by patching `Popen.wait` to give up after the tool has exited; it was not caught happening by chance.

`test_timeout_also_stops_what_the_tool_started` failed in that container while the host was busy, with `the child did not start before the timeout`. It failed the same way with `run.py` from `61924bb`, and it passed in the later run. Its 5 s timeout can be too short on a loaded emulated host. This fix does not change it.

Later GitHub Actions results are on the pull request.

## What is not shown

- **macOS with a real tool this user may not signal.** `sudo` here needs a password, which nothing here supplies. A `sudo` waiting at its prompt is no stand-in: `ps` showed this user's real user ID with effective user root, `killpg` succeeded and it exited with -9. No password was entered, and nothing ran as root. That output was not kept. macOS covers this path only with a faked `os.killpg`.
- **Which path `sudo` takes on GitHub's Linux runner.** The test accepts either: a member stays in the group and the error warns, or the group has gone and it does not. A `sudo` configured to run its command in its own session would leave the group, and this check could not see that. On a slow host, the 1 s timeout can also kill `sudo` before it starts its child, which takes the same path. The log does not say which path ran.
- **A tool in uninterruptible sleep.** It should be reported the same way, but none was run.
- **A false warning from a slow exit.** A tool that takes longer than the 1 s limit to exit after a signal is reported as possibly still running. A warning errs on the safe side.
- **Windows.** That branch still waits without a limit and reports the tool as stopped. It has never run.
- **A member that left the group.** It is not seen, as before.
- **The lock.** The run ends and releases the lock even if the tool could not be stopped. For `sudo` and a wholly unsignallable tool, it did before this fix too. **For a leader that survives a signal, this is new:** before the limit the run waited for that leader and kept the lock until it exited. With the limit, the lock goes about 1 s after the timeout while that leader may still be operating the board. The error warns about the tool and does not mention the lock, and after an interrupt only the tool's log warns. Nothing prevents the next run.
- Windows and hardware, anything.
