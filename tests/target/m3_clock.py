"""One process-shared host clock for UART, peers and temporary guard deadlines.

Python 3.9 on this Mac gives time.monotonic a different origin per process.
CLOCK_MONOTONIC is queried explicitly; unsupported hosts fail rather than
silently producing incomparable peer/receive timestamps. This clock is not UTC.
"""
import time as _native

sleep = _native.sleep


def monotonic():
    return _native.clock_gettime(_native.CLOCK_MONOTONIC)

CLOCK_NAME = 'clock_gettime(CLOCK_MONOTONIC)'


def identity():
    return dict(name=CLOCK_NAME, resolution_seconds=_native.clock_getres(_native.CLOCK_MONOTONIC))
