"""One locked run on one board. Opening a run performs no hardware operation."""
from __future__ import annotations

from dataclasses import asdict, dataclass
import os
import platform
from pathlib import Path
import re
import signal
import subprocess
import time
from typing import Any, Callable, List, Mapping, Optional, Sequence

from .console import Console, SerialTransport
from .errors import DeviceControlError, GateRefused, IdentityMismatch, ManifestError, ToolFailed
from .evidence import Evidence
from .lock import BoardLock
from .manifest import Manifest, sha256_file
from .profile import BoardProfile

Runner = Callable[[Sequence[str], Path, float], None]


def run_tool(command: Sequence[str], log_path: Path, timeout: float) -> None:
    """Runs an external tool without a shell. Its output goes only to the log."""
    with Path(log_path).open("ab") as log:
        try:
            process = subprocess.Popen(list(command), stdin=subprocess.DEVNULL, stdout=log,
                                       stderr=subprocess.STDOUT, start_new_session=os.name == "posix")
        except OSError as error:
            raise ToolFailed(f"cannot start {command[0]}: {error}") from error
        try:
            returncode = process.wait(timeout=timeout)
        except subprocess.TimeoutExpired as error:
            _stop(process)
            raise ToolFailed(f"{command[0]} timed out after {timeout} s") from error
        except BaseException:
            _stop(process)
            raise
    if returncode != 0:
        raise ToolFailed(f"{command[0]} failed with exit {returncode}; see {log_path}")


def _stop(process: subprocess.Popen) -> None:
    # The lock is released soon after this, so a wrapper's children must stop as well.
    if os.name == "posix":
        try:
            os.killpg(process.pid, signal.SIGKILL)
        except ProcessLookupError:
            pass
    else:
        process.kill()
    process.wait()


@dataclass(frozen=True)
class Gates:
    """Tool gates, not authority: the task must still authorise the hardware operation."""

    allow_reset: bool = False
    flash_approval: Optional[str] = None


class BoardRun:
    """Evidence first, so that every later refusal or failure is retained."""

    def __init__(self, profile: BoardProfile, manifest_path: Path, evidence_directory: Path,
                 gates: Gates = Gates(), transport=None, runner: Runner = run_tool,
                 inputs: Optional[Mapping[str, Any]] = None) -> None:
        self.profile = profile
        self.gates = gates
        self._manifest_path = Path(manifest_path)
        self._evidence_directory = Path(evidence_directory)
        self._transport = transport
        self._runner = runner
        self._inputs = inputs or {}
        self._identity: dict = {}
        self.manifest: Optional[Manifest] = None
        self.evidence: Optional[Evidence] = None
        self.console: Optional[Console] = None
        self._lock: Optional[BoardLock] = None
        self._active = False

    def __enter__(self) -> "BoardRun":
        self.evidence = Evidence(self._evidence_directory)
        try:
            self._start()
        except BaseException as error:
            self._finish(error)
            raise
        return self

    def __exit__(self, exception_type, exception, traceback) -> None:
        self._finish(exception)

    def _start(self) -> None:
        self.evidence.write_json("inputs.json", {
            **self._inputs,
            "profile": self.profile.as_record(),
            "gates": asdict(self.gates),
            "manifest_path": str(self._manifest_path),
            "host": {"platform": platform.platform(), "python": platform.python_version()},
        })
        # Written before the manifest is read: a null board means it could not be read.
        self._identity = {"manifest_sha256": None, "board": None, "source_revision": None,
                          "zephyr_revision": None, "build_id": None, "files": {},
                          "observed_sha256": {}, "observed_build_id": None}
        self.evidence.write_json("identity.json", self._identity)
        if self._manifest_path.is_file():
            self.evidence.copy_file(self._manifest_path, "manifest.json")
            self._identity["manifest_sha256"] = sha256_file(self._manifest_path)
            self.evidence.write_json("identity.json", self._identity)
        manifest = Manifest.load(self._manifest_path)
        self._identity.update(board=manifest.board, source_revision=manifest.source_revision,
                              zephyr_revision=manifest.zephyr_revision, build_id=manifest.build_id,
                              files=dict(manifest.files), observed_sha256=manifest.observed_hashes())
        self.evidence.write_json("identity.json", self._identity)
        manifest.verify(self.profile.board)
        if self.profile.identity_pattern and not manifest.build_id:
            raise ManifestError("the profile compares a printed identity, but the manifest has no build_id")
        self.manifest = manifest
        lock = BoardLock(self.profile.lock_directory, self.profile.board_id)
        lock.__enter__()
        self._lock = lock
        self.evidence.event("lock_acquired", board_id=self.profile.board_id)
        self.console = Console(self._transport or SerialTransport(self.profile), self.evidence)
        self._active = True

    def _finish(self, error: Optional[BaseException]) -> None:
        self._active = False
        cleanup_error = None
        # BaseException: an interrupt here must not skip the lock release or the result.
        if self.console is not None:
            try:
                self.console.retire()
            except BaseException as failure:
                cleanup_error = failure
        if self._lock is not None:
            lock, self._lock = self._lock, None
            try:
                lock.__exit__(None, None, None)
            except BaseException as failure:
                if cleanup_error is None or not isinstance(failure, Exception):
                    cleanup_error = failure
        failure = error or cleanup_error
        result = {"status": "passed" if failure is None else "failed"}
        if failure is not None:
            result.update(error_type=type(failure).__name__, error=str(failure))
        if error is not None and cleanup_error is not None:
            result["cleanup_error"] = repr(cleanup_error)
        try:
            self.evidence.write_json("result.json", result)
        finally:
            self.evidence.close()
        if cleanup_error is not None and (error is None or not isinstance(cleanup_error, Exception)):
            raise cleanup_error

    def _require_active(self) -> None:
        # Outside the context there is no lock and no evidence, so nothing may touch the board.
        if not self._active:
            raise DeviceControlError("this run is not active; use it inside its with block")

    def open_console(self) -> Console:
        self._require_active()
        self.console.open()
        return self.console

    def _refuse(self, operation: str, reason: str) -> None:
        self.evidence.event("refused", operation=operation, reason=reason)
        raise GateRefused(f"{operation} refused: {reason}")

    def _tool(self, operation: str, command: List[str], timeout: float) -> None:
        self.evidence.event("tool_start", operation=operation, command=command, timeout_seconds=timeout)
        started = time.monotonic()
        try:
            self._runner(command, self.evidence.log_path(f"{operation}.log"), timeout)
        except ToolFailed as error:
            self.evidence.event("tool_failed", operation=operation, error=str(error))
            raise
        self.evidence.event("tool_done", operation=operation, seconds=time.monotonic() - started)

    def reset(self) -> None:
        """Resets the board and waits for it to report ready. Never erases or programs."""
        self._require_active()
        if not self.gates.allow_reset:
            self._refuse("reset", "pass --allow-reset to perform it")
        if self.profile.reset is None:
            self._refuse("reset", "the board profile has no reset command")
        console = self.open_console()
        console.discard_pending()
        self._tool("reset", self.profile.reset.render(self.profile), self.profile.reset.timeout_seconds)
        self.confirm_boot()

    def confirm_boot(self) -> None:
        """Waits for the ready marker and compares any printed identity with the manifest."""
        console = self.open_console()
        deadline = time.monotonic() + self.profile.ready_timeout_seconds
        output = console.read_until(re.escape(self.profile.ready_marker.encode()),
                                    self.profile.ready_timeout_seconds)
        self.evidence.event("ready", marker=self.profile.ready_marker)
        if not self.profile.identity_pattern:
            return
        pattern = re.compile(self.profile.identity_pattern.encode())
        match = pattern.search(output)
        if match is None:
            remaining = max(0.0, deadline - time.monotonic())
            match = pattern.search(console.read_until(pattern, remaining))
        observed = match.group(1).decode("utf-8", errors="replace")
        self._identity["observed_build_id"] = observed
        self.evidence.write_json("identity.json", self._identity)
        if observed != self.manifest.build_id:
            raise IdentityMismatch(f"the board reports build {observed!r}, the manifest {self.manifest.build_id!r}")

    def flash(self, artefact: str) -> None:
        """Programs and verifies one manifest artefact. It does not reset or wait for boot."""
        self._require_active()
        approval = self.gates.flash_approval
        if not approval or not approval.strip():
            self._refuse("flash", "pass --allow-flash=<approval reference> to perform it")
        for name in ("flash", "verify"):
            if getattr(self.profile, name) is None:
                self._refuse("flash", f"the board profile has no {name} command")
        image = str(self.manifest.verified_path(artefact))
        self.evidence.event("flash_approved", approval=approval, artefact=artefact,
                            sha256=self.manifest.files[artefact])
        self._tool("flash", self.profile.flash.render(self.profile, image=image),
                   self.profile.flash.timeout_seconds)
        self._tool("verify", self.profile.verify.render(self.profile, image=image),
                   self.profile.verify.timeout_seconds)
        self.evidence.event("flash_verified", artefact=artefact)
