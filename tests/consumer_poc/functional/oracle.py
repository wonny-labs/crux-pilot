"""Application-owned functional oracle: it knows this application's console lines."""
import re

CYCLE = re.compile(r"^APP cycle=(\d+) skipped=(\d+)$")
DONE = re.compile(r"^APP done result=(-?\d+)$")


def check_schedule(lines, cycles=6, overrun_cycle=3, overrun_skips=2):
    """Raise AssertionError naming the first console line that breaks the schedule."""
    observed = [CYCLE.match(line) for line in lines]
    observed = [(int(found[1]), int(found[2])) for found in observed if found]
    assert [cycle for cycle, _ in observed] == list(range(1, cycles + 1)), f"cycles: {observed}"
    for cycle, skipped in observed:
        expected = overrun_skips if cycle == overrun_cycle else 0
        assert skipped == expected, f"cycle {cycle}: skipped {skipped}, expected {expected}"
    finished = [DONE.match(line) for line in lines]
    finished = [int(found[1]) for found in finished if found]
    assert finished == [0], f"completion results: {finished}"
