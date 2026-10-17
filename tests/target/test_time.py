"""M4 physical RTC liveness, boundaries and reset-session verification."""
from datetime import datetime, timezone
import time

SAFE = "2026-10-17T12:00:00"


def utc_seconds(text):
    return int(datetime.strptime(text, "%Y-%m-%dT%H:%M:%S UTC").replace(
        tzinfo=timezone.utc).timestamp())


def set_and_read(board, stamp):
    result, begin, _ = board.command("time set " + stamp)
    assert result == "OK"
    read, _, end = board.command("time get")
    delta = utc_seconds(read) - utc_seconds(stamp + " UTC")
    assert 0 <= delta <= (end - begin) + 1.0
    board.event(assertion="read-after-set", timestamp=stamp,
                delta_seconds=delta, transaction_interval_seconds=end - begin)
    return begin


def test_initial_unset(board):
    assert board.command("time get")[0] == "ERR NOT_SET"


def test_set_read_and_progression(board):
    set_and_read(board, SAFE)
    start = time.monotonic()
    values = []
    for index in range(4):
        time.sleep(max(0, start + index - time.monotonic()))
        remaining = start + 5.0 - time.monotonic()
        assert remaining > 0
        result, _, _ = board.command("time get", timeout=min(2.0, remaining))
        values.append(utc_seconds(result))
    elapsed = time.monotonic() - start
    assert elapsed <= 5.0
    assert values == sorted(values) and values[-1] > values[0]
    board.event(assertion="progression", values=values, elapsed_seconds=elapsed)


def test_rejected_inputs_preserve_time(board):
    begin = set_and_read(board, SAFE)
    cases = [
        ("time set 2023-02-29T12:00:00", "ERR INVALID_ARGUMENT"),
        ("time set 2026-13-01T00:00:00", "ERR INVALID_ARGUMENT"),
        ("time set 2026-10-17T12:00", "ERR INVALID_ARGUMENT"),
        ("time set 2026-10-17T12:00:00Z", "ERR INVALID_ARGUMENT"),
        ("time set 2026-10-17T12:00:00 extra", "ERR INVALID_ARGUMENT"),
        ("time set 1999-12-31T23:59:59", "ERR OUT_OF_RANGE"),
        ("time set 2100-01-01T00:00:00", "ERR OUT_OF_RANGE"),
    ]
    previous = utc_seconds(SAFE + " UTC")
    for command, expected in cases:
        assert board.command(command)[0] == expected
        result, _, end = board.command("time get")
        actual = utc_seconds(result)
        assert previous <= actual
        assert 0 <= actual - utc_seconds(SAFE + " UTC") <= (end - begin) + 1.0
        previous = actual


def test_post_2038_and_upper_range(board):
    try:
        set_and_read(board, "2038-01-19T03:14:08")
        set_and_read(board, "2099-12-31T23:59:50")
    finally:
        set_and_read(board, SAFE)


def test_reset_starts_unset(board):
    set_and_read(board, SAFE)
    board.reset()
    assert board.command("time get")[0] == "ERR NOT_SET"
    set_and_read(board, SAFE)
