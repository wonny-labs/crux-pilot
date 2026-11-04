"""One new evidence directory per run. Everything is flushed as it happens."""
from __future__ import annotations

from datetime import datetime, timezone
import json
from pathlib import Path
import shutil
import time
from typing import Any, Mapping

from .errors import EvidenceExistsError


class Evidence:
    def __init__(self, directory: Path) -> None:
        self.directory = Path(directory)
        try:
            self.directory.mkdir(parents=True, exist_ok=False)
        except FileExistsError as error:
            raise EvidenceExistsError(f"evidence directory already exists: {self.directory}") from error
        self._rx = (self.directory / "rx.bin").open("wb")
        self._events = (self.directory / "events.jsonl").open("w", encoding="utf-8")

    def event(self, name: str, **fields: Any) -> None:
        record = {"monotonic": time.monotonic(), "utc": datetime.now(timezone.utc).isoformat(),
                  "event": name, **fields}
        self._events.write(json.dumps(record, sort_keys=True) + "\n")
        self._events.flush()

    def rx(self, data: bytes) -> None:
        self._rx.write(data)
        self._rx.flush()
        self.event("rx", hex=data.hex())

    def tx(self, data: bytes) -> None:
        self.event("tx", hex=data.hex())

    def write_json(self, name: str, value: Mapping[str, Any]) -> None:
        (self.directory / name).write_text(json.dumps(value, indent=2, sort_keys=True) + "\n",
                                           encoding="utf-8")

    def copy_file(self, source: Path, name: str) -> None:
        shutil.copyfile(source, self.directory / name)

    def log_path(self, name: str) -> Path:
        return self.directory / name

    def close(self) -> None:
        self._rx.close()
        self._events.close()
