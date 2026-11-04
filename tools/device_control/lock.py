"""One OS-level lock per board ID. The OS releases it if the process dies.

The lock file is never deleted: removing it would let two runs lock different files.
"""
from __future__ import annotations

import errno
import os
from pathlib import Path
import sys
from urllib.parse import quote

from .errors import BoardLockHeld

_HELD_ERRNOS = {errno.EACCES, errno.EAGAIN, errno.EWOULDBLOCK, getattr(errno, "EDEADLOCK", errno.EAGAIN)}


def lock_path(directory: Path, board_id: str) -> Path:
    return Path(directory) / f"{quote(board_id, safe='')}.lock"


def _acquire_posix(stream) -> None:
    import fcntl
    fcntl.flock(stream.fileno(), fcntl.LOCK_EX | fcntl.LOCK_NB)


def _release_posix(stream) -> None:
    import fcntl
    fcntl.flock(stream.fileno(), fcntl.LOCK_UN)


def _acquire_windows(stream) -> None:
    import msvcrt
    stream.seek(0)
    msvcrt.locking(stream.fileno(), msvcrt.LK_NBLCK, 1)


def _release_windows(stream) -> None:
    import msvcrt
    stream.seek(0)
    msvcrt.locking(stream.fileno(), msvcrt.LK_UNLCK, 1)


def _acquire(stream) -> None:
    (_acquire_windows if sys.platform == "win32" else _acquire_posix)(stream)


def _release(stream) -> None:
    (_release_windows if sys.platform == "win32" else _release_posix)(stream)


class BoardLock:
    def __init__(self, directory: Path, board_id: str) -> None:
        self.path = lock_path(directory, board_id)
        self.board_id = board_id
        self._stream = None

    def __enter__(self) -> "BoardLock":
        self.path.parent.mkdir(parents=True, exist_ok=True)
        stream = self.path.open("a+b")
        try:
            _acquire(stream)
        except OSError as error:
            stream.close()
            if error.errno in _HELD_ERRNOS:
                raise BoardLockHeld(f"board {self.board_id!r} is held by another run ({self.path})") from error
            raise
        self._stream = stream
        return self

    def __exit__(self, *exception) -> None:
        stream, self._stream = self._stream, None
        try:
            _release(stream)
        finally:
            stream.close()
