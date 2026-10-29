"""Twister pytest harness probe of the existing Zephyr reference controller."""

from pathlib import Path
import sys

REPO = Path(__file__).resolve().parents[4]
sys.path.insert(0, str(REPO / "tests" / "controller_functional"))
from controller_scenario_oracle import assert_controller_scenario, corrupt_output_for_negative_test  # noqa: E402


def test_zephyr_controller_scenario(dut):
    lines = dut.readlines_until(regex=r"CONTROLLER_(?:COMPLETE|FAILED)", timeout=12)
    assert_controller_scenario(
        corrupt_output_for_negative_test("\n".join(lines)), composition="zephyr"
    )
