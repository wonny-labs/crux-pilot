"""Mac-hosted UART checks against an explicitly identified, already flashed image."""
import fcntl
import hashlib
import json
import platform
from datetime import datetime, timezone
import re
import subprocess
import termios
import time
from pathlib import Path

import pytest
import serial

REPO = Path(__file__).resolve().parents[2]
ANSI = re.compile(r"\x1b\[[0-?]*[ -/]*[@-~]")
RESULT = re.compile(r"(?:^|[\r\n])(OK|ERR [A-Z_]+|\d{4}-\d\d-\d\dT\d\d:\d\d:\d\d UTC)(?=[\r\n])")
READY = "CRUX_TIME_READY composition=zephyr"


def pytest_addoption(parser):
    group = parser.getgroup("physical-target")
    for name in ("uart", "elf", "manifest", "flash-log", "probe", "board", "evidence"):
        group.addoption(f"--{name}", default=None)


class Board:
    def __init__(self, port, output, probe):
        self.port = port
        self.output = output
        self.probe = probe
        self.raw = (output / "uart-rx.bin").open("wb")
        self.events = (output / "transactions.jsonl").open("w")

    def event(self, **data):
        self.events.write(json.dumps(dict(monotonic=time.monotonic(), **data)) + "\n")
        self.events.flush()

    def read(self):
        value = self.port.read(4096)
        if value:
            self.raw.write(value)
            self.raw.flush()
            self.event(direction="rx", hex=value.hex())
        return value

    def wait_for(self, predicate, timeout):
        end = time.monotonic() + timeout
        response = b""
        while time.monotonic() < end:
            response += self.read()
            if time.monotonic() > end:
                break
            text = ANSI.sub("", response.decode("utf-8", errors="replace"))
            result = predicate(text)
            if result is not None:
                return result
        raise AssertionError(f"UART timeout after {timeout}s; received {response!r}")

    def command(self, line, timeout=2.0):
        while self.port.in_waiting:
            self.read()
        started = time.monotonic()
        encoded = (line + "\r").encode("ascii")
        self.event(direction="tx", hex=encoded.hex(), command=line)
        self.port.write(encoded)
        self.port.flush()
        def result(text):
            lines = RESULT.findall(text)
            return lines[0] if lines else None
        value = self.wait_for(result, timeout)
        ended = time.monotonic()
        self.event(command=line, result=value, started=started, ended=ended,
                   duration_seconds=ended - started)
        return value, started, ended

    def reset(self):
        while self.port.in_waiting:
            self.read()
        self.event(action="reset", method="J-Link Commander r; g; q", probe=self.probe)
        command = ["/usr/local/bin/JLinkExe", "-USB", self.probe,
                   "-Device", "MIMXRT1062xxx6A", "-If", "SWD", "-Speed", "400",
                   "-NoGui", "1", "-ExitOnError", "1", "-AutoConnect", "1",
                   "-CommandFile", str(REPO / "tests/target/reset.jlink")]
        with (self.output / "reset.log").open("ab") as log:
            subprocess.run(command, stdout=log, stderr=subprocess.STDOUT,
                           check=True, timeout=20)
        self.wait_for(lambda text: READY if READY in text else None, 10.0)
        self.event(action="ready", marker=READY)


@pytest.fixture(scope="session")
def board(request):
    options = {key: request.config.getoption(f"--{key}") for key in
               ("uart", "elf", "manifest", "flash-log", "probe", "board", "evidence")}
    missing = [key for key, value in options.items() if not value]
    if missing:
        pytest.fail("Explicit physical inputs required: " + ", ".join(missing))
    assert options["board"] == "mimxrt1060_evk@B//qspi", "Only the verified EVKB QSPI setup is supported"
    output = Path(options["evidence"]).resolve()
    output.mkdir(parents=True, exist_ok=False)
    elf = Path(options["elf"]).resolve()
    digest = hashlib.sha256(elf.read_bytes()).hexdigest()
    manifest = json.loads(Path(options["manifest"]).read_text())
    expected = next(item["sha256"] for item in manifest["artifacts"]
                    if item["path"].endswith("/zephyr.elf"))
    assert digest == expected and manifest["target"] == options["board"]
    flash_log = Path(options["flash-log"]).read_bytes()
    assert b"Section rom_start" in flash_log and b"Section text" in flash_log
    assert flash_log.count(b"matched.") >= 2 and b"MIS-MATCHED" not in flash_log
    metadata = dict(options, executed_at_utc=datetime.now(timezone.utc).isoformat(),
                    host=platform.platform(), python=platform.python_version(),
                    pytest_version=pytest.__version__, pyserial_version=serial.__version__,
                    swd_speed_khz=400, elf_sha256=digest,
                    source=manifest["source"], build_id=manifest["build_id"],
                    flash_log_sha256=hashlib.sha256(flash_log).hexdigest(),
                    baud=115200, transaction_timeout_seconds=2,
                    readiness_timeout_seconds=10, rtc_tick_allowance_seconds=1)
    (output / "inputs.json").write_text(json.dumps(metadata, indent=2) + "\n")
    with serial.Serial(options["uart"], 115200, timeout=0.05,
                       write_timeout=2, exclusive=True) as port:
        # Kernel-enforced exclusivity also prevents a second UART program joining this run.
        fcntl.ioctl(port.fileno(), termios.TIOCEXCL)
        device = Board(port, output, options["probe"])
        try:
            device.reset()
            yield device
        finally:
            device.event(action="close")
            device.raw.close()
            device.events.close()
