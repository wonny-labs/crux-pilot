"""Board profile: everything board- or host-specific is data."""
from __future__ import annotations

from dataclasses import asdict, dataclass
import json
import math
import os
from pathlib import Path
import re
from typing import Any, List, Mapping, Optional, Tuple

from .errors import ProfileError

_PLACEHOLDER = re.compile(r"\{([^{}]*)\}")
# Only what is visible in the rendered arguments. A command file's contents are not seen.
_ERASE_OR_FUSE = re.compile(r"erase|fuse", re.IGNORECASE)
_BASE_PLACEHOLDERS = {"probe_serial", "mcu", "port", "board"}
_FIELDS = {"board_id", "board", "mcu", "port", "baud", "dtr", "rts", "ready_marker",
           "ready_timeout_seconds", "write_timeout_seconds", "lock_directory", "probe_serial",
           "identity_pattern", "reset", "flash", "verify"}


def _text(data: Mapping[str, Any], name: str) -> str:
    value = data.get(name)
    if not isinstance(value, str) or not value:
        raise ProfileError(f"{name} must be a non-empty string")
    return value


def _flag(data: Mapping[str, Any], name: str) -> bool:
    value = data.get(name)
    if not isinstance(value, bool):
        raise ProfileError(f"{name} must be true or false")
    return value


def _seconds(data: Mapping[str, Any], name: str) -> float:
    value = data.get(name)
    if isinstance(value, bool) or not isinstance(value, (int, float)):
        raise ProfileError(f"{name} must be a number of seconds")
    if not math.isfinite(value) or value <= 0:
        raise ProfileError(f"{name} must be positive and finite")
    return float(value)


@dataclass(frozen=True)
class ToolCommand:
    """An argument list run without a shell, with a bounded timeout."""

    command: Tuple[str, ...]
    timeout_seconds: float

    def render(self, profile: "BoardProfile", **values: str) -> List[str]:
        known = {"probe_serial": profile.probe_serial, "mcu": profile.mcu,
                 "port": profile.port, "board": profile.board, **values}
        return [_PLACEHOLDER.sub(lambda match: str(known[match.group(1)]), token)
                for token in self.command]


def _command(data: Mapping[str, Any], name: str, allowed: set, needs_image: bool) -> Optional[ToolCommand]:
    section = data.get(name)
    if section is None:
        return None
    if not isinstance(section, Mapping) or set(section) != {"command", "timeout_seconds"}:
        raise ProfileError(f"{name} must have exactly command and timeout_seconds")
    command = section["command"]
    if (not isinstance(command, list) or not command
            or not all(isinstance(token, str) and token for token in command)):
        raise ProfileError(f"{name}.command must be a non-empty list of strings")
    used = set()
    for token in command:
        if _ERASE_OR_FUSE.search(token):
            raise ProfileError(f"{name}.command must not perform an erase or fuse operation: {token!r}")
        used.update(_PLACEHOLDER.findall(token))
    unknown = used - allowed
    if unknown:
        raise ProfileError(f"{name}.command has an unknown placeholder: {sorted(unknown)}")
    for placeholder in used - {"image"}:
        if _ERASE_OR_FUSE.search(str(data.get(placeholder, ""))):
            raise ProfileError(f"{name}.command must not perform an erase or fuse operation: "
                               f"{placeholder} is {data[placeholder]!r}")
    if needs_image and "image" not in used:
        raise ProfileError(f"{name}.command must name the checked image with {{image}}")
    if "probe_serial" in used and not data.get("probe_serial"):
        raise ProfileError(f"{name}.command uses probe_serial, which the profile does not set")
    return ToolCommand(tuple(command), _seconds(section, "timeout_seconds"))


@dataclass(frozen=True)
class BoardProfile:
    board_id: str
    board: str
    mcu: str
    port: str
    baud: int
    dtr: bool
    rts: bool
    ready_marker: str
    ready_timeout_seconds: float
    write_timeout_seconds: float
    lock_directory: str
    probe_serial: Optional[str] = None
    identity_pattern: Optional[str] = None
    reset: Optional[ToolCommand] = None
    flash: Optional[ToolCommand] = None
    verify: Optional[ToolCommand] = None

    @classmethod
    def from_mapping(cls, data: Mapping[str, Any]) -> "BoardProfile":
        unknown = set(data) - _FIELDS
        if unknown:
            raise ProfileError(f"unknown profile fields: {sorted(unknown)}")
        baud = data.get("baud")
        if isinstance(baud, bool) or not isinstance(baud, int) or baud <= 0:
            raise ProfileError("baud must be a positive integer")
        lock_directory = _text(data, "lock_directory")
        if not os.path.isabs(lock_directory):
            raise ProfileError("lock_directory must be absolute, so every run finds the same lock")
        probe_serial = data.get("probe_serial")
        if probe_serial is not None and (not isinstance(probe_serial, str) or not probe_serial):
            raise ProfileError("probe_serial must be a non-empty string")
        identity = data.get("identity_pattern")
        if identity is not None:
            if not isinstance(identity, str):
                raise ProfileError("identity_pattern must be a string")
            try:
                groups = re.compile(identity.encode()).groups
            except re.error as error:
                raise ProfileError(f"identity_pattern is not a valid expression: {error}") from error
            if groups != 1:
                raise ProfileError("identity_pattern must have exactly one group")
        return cls(
            board_id=_text(data, "board_id"), board=_text(data, "board"), mcu=_text(data, "mcu"),
            port=_text(data, "port"), baud=baud, dtr=_flag(data, "dtr"), rts=_flag(data, "rts"),
            ready_marker=_text(data, "ready_marker"),
            ready_timeout_seconds=_seconds(data, "ready_timeout_seconds"),
            write_timeout_seconds=_seconds(data, "write_timeout_seconds"),
            lock_directory=lock_directory, probe_serial=probe_serial, identity_pattern=identity,
            reset=_command(data, "reset", _BASE_PLACEHOLDERS, False),
            flash=_command(data, "flash", _BASE_PLACEHOLDERS | {"image"}, True),
            verify=_command(data, "verify", _BASE_PLACEHOLDERS | {"image"}, True))

    @classmethod
    def from_file(cls, path: Path) -> "BoardProfile":
        try:
            data = json.loads(Path(path).read_text(encoding="utf-8"))
        except (OSError, ValueError) as error:
            raise ProfileError(f"cannot read board profile {path}: {error}") from error
        if not isinstance(data, dict):
            raise ProfileError(f"board profile {path} must be a JSON object")
        return cls.from_mapping(data)

    def as_record(self) -> dict:
        return asdict(self)
