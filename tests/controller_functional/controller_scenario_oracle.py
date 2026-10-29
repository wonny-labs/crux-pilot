"""Bounded output oracle for the existing 20-cycle reference controller."""

import os
import re

_ROW = re.compile(
    r"cycle=(\d+) status=(-?\d+) valid=(\d) utc=(\S+) "
    r"monotonic_ms=(\d+) lateness_ms=(\d+)"
)


def corrupt_output_for_negative_test(output: str) -> str:
    """Change one observed field only when explicitly running the negative PoC."""
    if os.environ.get("CRUX_PYTEST_BAD_OUTPUT") != "1":
        return output
    original = "cycle=6 status=0 valid=1"
    assert output.count(original) == 1, "Cannot inject controlled cycle-6 result error"
    return output.replace(original, "cycle=6 status=0 valid=0", 1)


def assert_controller_scenario(output: str, *, composition: str) -> None:
    """Check observed behaviour, not merely process completion or a success marker."""
    assert f"controller composition={composition} init=0" in output, output
    assert "CONTROLLER_FAILED" not in output, output
    assert "CONTROLLER_COMPLETE cycles=20" in output, output

    rows = _ROW.findall(output)
    assert len(rows) == 20, f"Expected 20 cycle observations, got {len(rows)}\n{output}"

    last_monotonic = -1
    for index, (cycle, status, valid, utc, monotonic, _) in enumerate(rows):
        assert int(cycle) == index, f"cycle index {index}: observed {cycle}"
        assert int(monotonic) > last_monotonic, f"cycle {index}: monotonic clock did not advance"
        last_monotonic = int(monotonic)

        if index < 2:
            assert (status, valid, utc) == ("-4", "0", "unavailable"), (
                f"cycle {index}: expected unset Time, got {status=}, {valid=}, {utc=}"
            )
        else:
            assert status == "0" and valid == "1" and utc.isdigit(), (
                f"cycle {index}: expected valid Time, got {status=}, {valid=}, {utc=}"
            )
            if composition == "host-controlled":
                expected_utc = 1792238400 if index < 6 else 1792234800
                assert int(utc) == expected_utc, (
                    f"cycle {index}: expected UTC {expected_utc}, got {utc}"
                )

    assert int(rows[6][3]) < int(rows[5][3]), "Cycle 6 did not step UTC backwards"
