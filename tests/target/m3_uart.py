"""Exact replay of retained UART events without text repair."""
import json
import math
from pathlib import Path


def exact_event_rows(directory):
    """Rebuild exact UART lines with receive times; no ANSI/whitespace correction."""
    directory = Path(directory)
    stream = bytearray()
    pending = b''
    rows = []
    previous = -math.inf
    for line in (directory / 'receive.jsonl').read_text().splitlines():
        event = json.loads(line)
        stamp = event['monotonic']
        if type(stamp) not in (int, float) or not math.isfinite(stamp) or stamp < previous:
            raise ValueError('UART event clock invalid')
        previous = stamp
        chunk = bytes.fromhex(event['hex'])
        stream.extend(chunk)
        pending += chunk
        while b'\n' in pending:
            record, pending = pending.split(b'\n', 1)
            if record.endswith(b'\r'):
                record = record[:-1]
            rows.append((stamp, record.decode('ascii')))
    if pending or bytes(stream) != (directory / 'uart-rx.bin').read_bytes():
        raise ValueError('Incomplete or mismatched exact UART event stream')
    return rows
