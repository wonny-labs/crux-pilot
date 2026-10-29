"""Functional pytest probe against the already-built Linux reference controller."""

import os
from pathlib import Path
import subprocess

from controller_scenario_oracle import assert_controller_scenario, corrupt_output_for_negative_test

REPO = Path(__file__).resolve().parents[2]
DEFAULT_BINARY = REPO / "build/linux/debug/samples/controller/host/crux-controller-host"


def test_host_controller_scenario():
    binary = Path(os.environ.get("CRUX_PYTEST_HOST_BINARY", str(DEFAULT_BINARY))).resolve()
    assert binary.is_file(), f"Build the Host reference controller first: {binary}"
    result = subprocess.run(
        [str(binary)], stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
        text=True, timeout=12, check=False,
    )
    assert result.returncode == 0, (
        f"Host reference controller exited with {result.returncode}:\n{result.stdout}"
    )
    assert_controller_scenario(
        corrupt_output_for_negative_test(result.stdout), composition="host-controlled"
    )
