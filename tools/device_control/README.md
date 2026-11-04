# Device-control tool

Test tooling for the machine that holds a board's probe and UART. It implements the
[device-control contract](../../docs/test-foundation-proposal.md#device-control-contract).

**Status: internal until a second consumer uses it, and never run against hardware.** It can change
without compatibility work. It is a plain Python package outside the runtime CMake and adds nothing
to a Crux image.

**The flags are a tool gate, not authority.** [`AGENTS.md`](../../AGENTS.md) still requires explicit
authorisation of hardware operations for the current task. `--allow-flash` only records the reference.

## What it does

| Item | Behaviour |
| --- | --- |
| Opening a run | Performs no hardware operation. It creates the evidence directory, checks the manifest and takes the lock, in that order |
| `reset()` | Off unless `--allow-reset` is given. Discards earlier console output, runs the profile's reset command with its timeout, then waits for the ready marker. Never erases or programs |
| `flash(artefact)` | Refused unless `--allow-flash=<approval reference>` is given and the profile has both a flash and a verify command. Rechecks the artefact's hash, runs flash, then verify. It does not reset or wait for boot; reset separately, under its own gate |
| `confirm_boot()` | Waits for the ready marker and, if the profile sets `identity_pattern`, compares the printed identity with the manifest's `build_id` |
| Refusals | A refused gate returns before the port opens, because opening a port can reset some boards. Reset and flash also refuse outside the run's `with` block |
| Console | `open`, `read_until(pattern, timeout)` and `write`. Raw bytes, exclusive open, DTR and RTS from the profile. Timeouts use `time.monotonic()`. Knows no application protocol. Opening it has no gate of its own: `reset()` and an explicit `open_console()` or `confirm_boot()` open it, so check the DTR and RTS settings first. It refuses to open once the run ends, because the lock has gone |
| Lock | One OS-level lock per board ID for the whole run. A held lock fails at once. The OS releases it if the process dies |
| Evidence | A new directory per run; it fails if the directory exists. Keeps `rx.bin`, `events.jsonl`, `inputs.json`, `manifest.json`, `identity.json`, one `<operation>.log` per tool and `result.json`, including after a failure. `identity.json` holds the expected and observed hashes; a null `board` means the manifest could not be read. A missing, empty or partial `result.json` means the process died, or that cleanup failed or was interrupted before the file was complete. `result.json` reports the `with` block: a refusal that a test catches still shows as passed, and appears in `events.jsonl` |

## Inputs

The board profile is JSON and holds everything specific to a board or host. Every field below is
required except `probe_serial`, `identity_pattern` and the three commands. Unknown fields are rejected.

```json
{
  "board_id": "bench-a/board-1",
  "board": "vendor_board@B//qspi",
  "mcu": "EXAMPLE_MCU",
  "port": "/dev/ttyACM0",
  "baud": 115200,
  "dtr": false,
  "rts": false,
  "ready_marker": "APP READY",
  "ready_timeout_seconds": 10,
  "write_timeout_seconds": 2,
  "lock_directory": "/var/tmp/crux-board-locks",
  "probe_serial": "000000000",
  "identity_pattern": "BUILD_ID=(\\S+)",
  "reset": {"command": ["board-tool", "reset", "--probe", "{probe_serial}"], "timeout_seconds": 20},
  "flash": {"command": ["board-tool", "program", "--probe", "{probe_serial}", "{image}"], "timeout_seconds": 120},
  "verify": {"command": ["board-tool", "compare", "--probe", "{probe_serial}", "{image}"], "timeout_seconds": 120}
}
```

- `board_id` names one physical board and is the lock's identity. `board` is the build target and must equal the manifest's `board`.
- `lock_directory` must be absolute and the same for every run on the tool host.
- Commands are argument lists run without a shell. Placeholders are `{probe_serial}`, `{mcu}`, `{port}`, `{board}` and, for flash and verify only, `{image}`, which both must use.

The manifest sits in the hand-off directory beside the files it lists. A run needs one even for a
reset, so its evidence stays tied to an exact image. `build_id` is required only with `identity_pattern`.

```json
{
  "schema_version": 1,
  "board": "vendor_board@B//qspi",
  "source_revision": "0000000000000000000000000000000000000000",
  "zephyr_revision": "0000000000000000000000000000000000000000",
  "build_id": "example-build",
  "files": [
    {"path": "zephyr.hex", "sha256": "0000000000000000000000000000000000000000000000000000000000000000"}
  ]
}
```

Nothing writes this manifest yet. A build host produces it, and
`scripts/record-build-provenance.py` records similar data for Crux's own images.

## Use

```sh
python3 -m pip install -r tools/device_control/requirements.txt
export PYTHONPATH=<crux>/tools
python3 -m device_control --profile board.json --manifest handoff/manifest.json \
  --evidence <new-directory> reset --allow-reset
python3 -m device_control --profile board.json --manifest handoff/manifest.json \
  --evidence <another-new-directory> flash zephyr.hex --allow-flash=<approval reference>
```

From a test on the tool host, with gates that the test's own options set:

```python
from device_control import BoardProfile, BoardRun, Gates

profile = BoardProfile.from_file("board.json")
with BoardRun(profile, "handoff/manifest.json", evidence_directory, Gates(allow_reset=True)) as run:
    run.reset()
    run.console.write(b"status\r")
    output = run.console.read_until(rb"OK\r?\n", timeout=2)
```

## Tests and what they do not show

`tests/build/test_device_control.py` uses fake transports and scripted tools. The Linux test build
discovers it, and the [recorded runs](../../docs/evidence/test-foundation-tool/README.md) give the
revisions and results. On macOS, run only this file, because the other build tests need Linux:

```sh
python3 -m unittest discover -s tests/build -p test_device_control.py -v
```

| Environment | Locking and evidence | Status |
| --- | --- | --- |
| Linux | `flock` across real processes, including a killed holder | Run |
| macOS | The same | Run |
| Windows | `msvcrt.locking` branch against a fake module only | **Not run.** No locking, evidence, console, process or path behaviour has been exercised on Windows |

Also not shown:

- **Nothing has run on hardware**: no real serial port, probe, reset, flash or `west flash`. One test uses real pyserial on a disposable pseudo-terminal when pyserial is installed; a pseudo-terminal has no DTR or RTS lines.
- **DTR and RTS are set before the port opens**, but an operating system or USB bridge can still move those lines during open. A new board needs this checked before its profile is trusted.
- **The erase and fuse check reads only the rendered command arguments, without the image path.** A script or command file a profile names can still do either, and so can an inline `sh -c` or an option split across arguments, so the profile needs review.
- **The tool does not verify that a command does what its name says.** The verify command's result is only as good as that command.
- **A flash or reset tool can outlive the lock.** If this process is killed while a tool runs, the OS releases the lock and the tool may still be operating the board. On a timeout, the tool's process group is stopped on POSIX, but a program that leaves that group survives, and on Windows only the tool itself is stopped.
- **Stopping a tool can raise the wrong error.** On macOS, if a tool exits just before its timeout or an interrupt stops it, `os.killpg` raises `PermissionError`. The run still fails and releases the lock, but with that error instead of `ToolFailed` or the interrupt. A tool this user may not signal, such as one run through `sudo`, could give the same error on any POSIX host while it keeps running; that was not probed. The [recorded runs](../../docs/evidence/test-foundation-tool/README.md#stopping-a-tool-on-macos-a-known-defect-not-fixed) hold the probe. Not fixed.
- **Cleanup is not fully safe from interrupts.** An interrupt raised inside the console close or the lock release no longer skips the rest. One that lands between those steps, or during the write of `result.json`, still can, and one that lands as the port opens can leave it open past the lock until the process exits. The OS releases the lock and the port when the process exits.
- **The lock only excludes runs that agree on it.** Two profiles with different `lock_directory` or `board_id` values for one board do not exclude each other, and `flock` on a shared or virtual machine filesystem was not tested.
- **A reset can still see an old ready marker.** Earlier output is discarded for up to one second before the reset command starts. A marker printed after that, before the board actually resets, satisfies the wait, so choose a marker the application prints only once per boot.
- **A file can change between the hash check and the flash tool reading it.** The hand-off directory must stay private to the run.
- **Only `from_file` and `from_mapping` validate a profile.** Building `BoardProfile` or `ToolCommand` directly skips those checks.
