"""Offline oracle for the M5 physical Serial fixture (Group B, issue #27).

The fixture exercises the portable Serial contract over LPUART3 in the peripheral's internal
loopback mode (devicetree nxp,loopback). A PASS is marked ``limited``: the LPUART3 transmitter,
receiver, status flags, the Zephyr driver and the Crux Serial provider are exercised, but the
board pins, connector, electrical path and an external peer are NOT. It never talks to hardware.
"""
import re
import sys
from pathlib import Path

PAYLOAD_BYTES = 16
TIMEOUT_MS = 300
TIMEOUT_STATUS = -7  # CRUX_ERR_TIMEOUT
TIMEOUT_MAX_MS = 1500
MODE = 'internal_loopback'
LIMITS = 'internal_loopback_pins_not_exercised'
ANSI = re.compile(r'\x1b\[[0-9;]*[A-Za-z]')
RECORD = re.compile(r'm5-serial: (M5B_[A-Z_]+_V1)((?: [a-z_]+=\S+)*)\s*$')


def records(data):
    """Complete (newline-terminated) M5B records in capture order."""
    text = ANSI.sub('', data.decode('utf-8', errors='replace'))
    rows = []
    for raw in text.split('\n')[:-1]:
        match = RECORD.search(raw.rstrip('\r'))
        if match:
            rows.append((match.group(1), dict(item.split('=', 1) for item in match.group(2).split())))
    return rows


def num(fields, key):
    try:
        return int(fields[key], 0)
    except (KeyError, ValueError):
        return None


def verdict(kind, reasons, **extra):
    return dict(verdict=kind, reasons=list(reasons), limited=True, **extra)


def check_transfer(fields, seq, reasons):
    ok = (num(fields, 'seq') == seq and num(fields, 'write_status') == 0
          and num(fields, 'written') == PAYLOAD_BYTES and num(fields, 'read_status') == 0
          and num(fields, 'read') == PAYLOAD_BYTES and num(fields, 'match') == 1)
    if not ok:
        reasons.append('transfer %d failed or mismatched' % seq)
    return ok


def evaluate(data):
    rows = records(data)
    names = [name for name, _ in rows]
    if not rows:
        return verdict('INCONCLUSIVE', ['no M5B record'])
    if names.count('M5B_BEGIN_V1') > 1 or names.count('M5B_RESULT_V1') > 1:
        return verdict('FAIL', ['more than one Serial run in the capture'])
    if 'M5B_BEGIN_V1' not in names:
        return verdict('INCONCLUSIVE', ['no begin record'])
    reasons = []
    by_name = {}
    for index, (name, fields) in enumerate(rows):
        by_name.setdefault(name, []).append((index, fields))
    begin = by_name['M5B_BEGIN_V1'][0][1]
    if begin.get('mode') != MODE or begin.get('port') != 'lpuart3':
        return verdict('FAIL', ['unexpected port or mode'])
    if num(begin, 'open_status') not in (None, 0):
        return verdict('FAIL', ['Serial open failed'])
    if num(begin, 'open_status') is None:
        return verdict('INCONCLUSIVE', ['begin record lacks open_status'])
    explicit = by_name.get('M5B_RESULT_V1')
    if explicit and explicit[0][1].get('result') == 'FAIL':
        return verdict('FAIL', ['fixture reported FAIL: ' + explicit[0][1].get('reason', 'unspecified')])
    transfers = by_name.get('M5B_XFER_V1', [])
    timeouts = by_name.get('M5B_TIMEOUT_V1', [])
    failed = False
    if len(timeouts) > 1 or len(transfers) > 2:
        return verdict('FAIL', ['too many transfer or timeout records'])
    for _, fields in transfers:
        seq = num(fields, 'seq')
        if seq not in (1, 2) or not check_transfer(fields, seq, reasons):
            failed = True
    seqs = [num(f, 'seq') for _, f in transfers]
    if timeouts:
        _, t = timeouts[0]
        if (num(t, 'read_status') != TIMEOUT_STATUS or num(t, 'transferred') != 0
                or num(t, 'timeout_ms') != TIMEOUT_MS):
            reasons.append('timeout case did not return CRUX_ERR_TIMEOUT with no data')
            failed = True
        elapsed = num(t, 'elapsed_ms')
        if elapsed is None or elapsed < TIMEOUT_MS or elapsed > TIMEOUT_MAX_MS:
            reasons.append('timeout elapsed outside bounds')
            failed = True
        if len(transfers) == 2 and not (transfers[0][0] < timeouts[0][0] < transfers[1][0]):
            reasons.append('records are not transfer, timeout, transfer')
            failed = True
    if len(seqs) == 2 and seqs != [1, 2]:
        reasons.append('transfer sequence out of order')
        failed = True
    close = by_name.get('M5B_CLOSE_V1')
    if close and any(num(close[0][1], k) != 0 for k in ('close_status', 'reopen_status', 'reclose_status')):
        reasons.append('close or reopen failed')
        failed = True
    if failed:
        return verdict('FAIL', reasons)
    if len(transfers) < 2 or not timeouts or not close:
        return verdict('INCONCLUSIVE', reasons + ['incomplete Serial sequence'])
    result = by_name.get('M5B_RESULT_V1')
    if not result:
        return verdict('INCONCLUSIVE', reasons + ['no result record'])
    fields = result[0][1]
    if fields.get('result') != 'PASS':
        return verdict('INCONCLUSIVE', reasons + ['result line is not PASS'])
    if fields.get('limits') != LIMITS:
        return verdict('INCONCLUSIVE', reasons + ['result line lacks the limited-evidence label'])
    return verdict('PASS', reasons, transfers=len(transfers))


if __name__ == '__main__':
    out = evaluate(Path(sys.argv[1]).read_bytes())
    print(out)
    sys.exit(0 if out['verdict'] == 'PASS' else 1)
