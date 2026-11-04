"""Functional level on native_sim. No hardware, flash or reset; not hardware evidence."""
from oracle import check_schedule


def test_periodic_schedule(dut, image_identity):
    assert dut.device_config.type == "native", "this PoC never targets a device"
    assert image_identity["sha256"]
    check_schedule(dut.readlines_until(regex=r"^APP done ", timeout=15))
