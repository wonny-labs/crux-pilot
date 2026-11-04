"""Command line for the two gated operations. See README.md."""
from __future__ import annotations

import argparse
from pathlib import Path
import sys
from typing import List, Optional

from .errors import DeviceControlError
from .profile import BoardProfile
from .run import BoardRun, Gates, Runner, run_tool


def _parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(prog="device_control", description=__doc__)
    parser.add_argument("--profile", required=True, type=Path, help="board profile (JSON)")
    parser.add_argument("--manifest", required=True, type=Path, help="hand-off manifest (JSON)")
    parser.add_argument("--evidence", required=True, type=Path, help="new evidence directory")
    operations = parser.add_subparsers(dest="operation", required=True)
    reset = operations.add_parser("reset", help="reset the board and wait for its ready marker")
    reset.add_argument("--allow-reset", action="store_true", help="enable the reset, off by default")
    flash = operations.add_parser("flash", help="program and verify one manifest artefact")
    flash.add_argument("artefact", help="file name as listed in the manifest")
    flash.add_argument("--allow-flash", metavar="APPROVAL_REFERENCE", help="enable flashing and record its approval")
    return parser


def main(argv: Optional[List[str]] = None, transport=None, runner: Runner = run_tool) -> int:
    arguments = _parser().parse_args(argv)
    try:
        gates = Gates(allow_reset=getattr(arguments, "allow_reset", False),
                      flash_approval=getattr(arguments, "allow_flash", None))
        profile = BoardProfile.from_file(arguments.profile)
        inputs = {"operation": arguments.operation, "artefact": getattr(arguments, "artefact", None)}
        with BoardRun(profile, arguments.manifest, arguments.evidence, gates=gates, transport=transport,
                      runner=runner, inputs=inputs) as run:
            if arguments.operation == "reset":
                run.reset()
            else:
                run.flash(arguments.artefact)
    except DeviceControlError as error:
        print(f"error: {error}", file=sys.stderr)
        return 1
    return 0
