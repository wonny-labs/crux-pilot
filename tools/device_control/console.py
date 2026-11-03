"""Board console. Raw bytes only: applications own every protocol and oracle."""
from __future__ import annotations

import os
import re
import time
from typing import Optional, Pattern, Union

from .errors import ConsoleError, ConsoleTimeout

_READ_SIZE = 4096
_READ_POLL_SECONDS = 0.05
_DISCARD_LIMIT_SECONDS = 1.0


class SerialTransport:
    """pyserial with the profile's DTR and RTS, which must be set before the port opens."""

    def __init__(self, profile) -> None:
        self._profile = profile
        self._port = None

    def open(self) -> None:
        try:
            import serial
        except ImportError as error:
            raise ConsoleError("the console needs pyserial; see tools/device_control/requirements.txt") from error
        port = serial.Serial()
        port.baudrate = self._profile.baud
        port.timeout = _READ_POLL_SECONDS
        port.write_timeout = self._profile.write_timeout_seconds
        port.exclusive = True
        port.dtr = self._profile.dtr
        port.rts = self._profile.rts
        port.port = self._profile.port
        try:
            port.open()
        except (serial.SerialException, OSError) as error:
            raise ConsoleError(f"cannot open {self._profile.port}: {error}") from error
        self._port = port
        if os.name == "posix":
            self._deny_other_programs(port)

    def _deny_other_programs(self, port) -> None:
        # pyserial's exclusive uses flock, which only stops programs that also use flock.
        import fcntl
        import termios
        try:
            fcntl.ioctl(port.fileno(), termios.TIOCEXCL)
        except OSError as error:
            self.close()
            raise ConsoleError(f"cannot get exclusive use of {self._profile.port}: {error}") from error

    def close(self) -> None:
        if self._port is not None:
            port, self._port = self._port, None
            port.close()

    def read(self, size: int) -> bytes:
        return self._port.read(size)

    def write(self, data: bytes) -> None:
        self._port.write(data)


class Console:
    def __init__(self, transport, evidence=None) -> None:
        self._transport = transport
        self._evidence = evidence
        self._open = False
        self._pending = b""

    def open(self) -> None:
        if not self._open:
            self._transport.open()
            self._open = True
            if self._evidence:
                self._evidence.event("console_open")

    def close(self) -> None:
        if self._open:
            self._open = False
            self._transport.close()
            if self._evidence:
                self._evidence.event("console_close")

    def _require_open(self) -> None:
        if not self._open:
            raise ConsoleError("the console is not open")

    def _read_chunk(self) -> bytes:
        chunk = self._transport.read(_READ_SIZE)
        if chunk and self._evidence:
            self._evidence.rx(chunk)
        return chunk

    def write(self, data: bytes) -> None:
        self._require_open()
        if not isinstance(data, (bytes, bytearray)):
            raise TypeError("console data must be bytes")
        if self._evidence:
            self._evidence.tx(bytes(data))
        self._transport.write(bytes(data))

    def read_until(self, pattern: Union[str, bytes, Pattern[bytes]], timeout: float) -> bytes:
        """Returns bytes through the match and keeps the rest for the next read."""
        self._require_open()
        if isinstance(pattern, str):
            pattern = pattern.encode()
        matcher = re.compile(pattern) if isinstance(pattern, bytes) else pattern
        deadline = time.monotonic() + timeout
        while True:
            match = matcher.search(self._pending)
            if match:
                result, self._pending = self._pending[:match.end()], self._pending[match.end():]
                return result
            self._pending += self._read_chunk()
            match = matcher.search(self._pending)
            if not match and time.monotonic() >= deadline:
                raise ConsoleTimeout(f"no match for {matcher.pattern!r} within {timeout} s; "
                                     f"received {len(self._pending)} bytes", self._pending)

    def discard_pending(self, limit: Optional[float] = None) -> None:
        """Drops output that predates an operation. It stays in the evidence."""
        self._require_open()
        self._pending = b""
        deadline = time.monotonic() + (_DISCARD_LIMIT_SECONDS if limit is None else limit)
        while self._read_chunk() and time.monotonic() < deadline:
            pass
