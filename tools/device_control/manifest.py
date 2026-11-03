"""Hand-off manifest: which exact files, for which board, from which revisions."""
from __future__ import annotations

from dataclasses import dataclass
import hashlib
import json
from pathlib import Path, PurePosixPath
import re
from typing import Any, Dict, Mapping, Optional

from .errors import ManifestError

_SHA256 = re.compile(r"[0-9a-f]{64}")


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with Path(path).open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def _required_text(data: Mapping[str, Any], name: str) -> str:
    value = data.get(name)
    if not isinstance(value, str) or not value:
        raise ManifestError(f"manifest {name} must be a non-empty string")
    return value


def _checked_name(name: Any) -> str:
    if not isinstance(name, str) or not name or "\\" in name or ":" in name:
        raise ManifestError(f"manifest path is not a relative POSIX path: {name!r}")
    path = PurePosixPath(name)
    if path.is_absolute() or ".." in path.parts:
        raise ManifestError(f"manifest path leaves the hand-off directory: {name!r}")
    return name


@dataclass(frozen=True)
class Manifest:
    path: Path
    board: str
    source_revision: str
    zephyr_revision: str
    build_id: Optional[str]
    files: Mapping[str, str]

    @classmethod
    def load(cls, path: Path) -> "Manifest":
        path = Path(path)
        try:
            data = json.loads(path.read_text(encoding="utf-8"))
        except (OSError, ValueError) as error:
            raise ManifestError(f"cannot read manifest {path}: {error}") from error
        if not isinstance(data, dict) or data.get("schema_version") != 1:
            raise ManifestError(f"manifest {path} must be a schema_version 1 object")
        entries = data.get("files")
        if not isinstance(entries, list) or not entries:
            raise ManifestError("manifest files must be a non-empty list")
        files: Dict[str, str] = {}
        for entry in entries:
            if not isinstance(entry, dict):
                raise ManifestError("each manifest file must be an object")
            name = _checked_name(entry.get("path"))
            digest = entry.get("sha256")
            if not isinstance(digest, str) or not _SHA256.fullmatch(digest):
                raise ManifestError(f"{name}: sha256 must be 64 lowercase hexadecimal digits")
            if name in files:
                raise ManifestError(f"duplicate manifest path: {name}")
            files[name] = digest
        build_id = data.get("build_id")
        if build_id is not None and (not isinstance(build_id, str) or not build_id):
            raise ManifestError("manifest build_id must be a non-empty string")
        return cls(path=path, board=_required_text(data, "board"),
                   source_revision=_required_text(data, "source_revision"),
                   zephyr_revision=_required_text(data, "zephyr_revision"),
                   build_id=build_id, files=files)

    def verify(self, board: str) -> None:
        """Checks the board and every listed file."""
        if self.board != board:
            raise ManifestError(f"manifest board {self.board!r} does not match profile board {board!r}")
        for name in self.files:
            self.verified_path(name)

    def verified_path(self, name: str) -> Path:
        """Returns a listed file's real path, after checking its location and hash."""
        if name not in self.files:
            raise ManifestError(f"{name} is not listed in the manifest")
        root = self.path.resolve().parent
        path = (root / name).resolve()
        if root not in path.parents:
            raise ManifestError(f"{name} leaves the hand-off directory")
        try:
            digest = sha256_file(path)
        except OSError as error:
            raise ManifestError(f"cannot read {name}: {error}") from error
        if digest != self.files[name]:
            raise ManifestError(f"{name} does not match its manifest hash")
        return path
