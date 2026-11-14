"""Offline oracle for the M5 physical watchdog/reset fixture (Group D, issue #30).

Reads retained raw UART bytes and returns PASS, FAIL or INCONCLUSIVE. It never talks to
hardware. PASS needs one complete arm boot (start, five good feeds, deliberate feed stop)
followed by exactly one report boot that observed the latched WATCHDOG reset cause and did
not start the watchdog again. A missing, truncated or unsupported observation is
INCONCLUSIVE; an error status, wrong count or repeated activation is FAIL.
"""
import re
import sys
from pathlib import Path

WATCHDOG_BIT = 1 << 3
START_TIMEOUT_MS = 2000
FEED_COUNT = 5
IDLE, ACTIVE = 1, 2
ANSI = re.compile(r'\x1b\[[0-9;]*[A-Za-z]')
RECORD = re.compile(r'm5-watchdog: (M5D_[A-Z_]+_V1)((?: [a-z_]+=\S+)*)\s*$')


def records(data):
    """Complete (newline-terminated) M5D records in capture order."""
    text = ANSI.sub('', data.decode('utf-8', errors='replace'))
    complete = text.split('\n')[:-1]
    rows = []
    for raw in complete:
        match = RECORD.search(raw.rstrip('\r'))
        if match:
            fields = dict(item.split('=', 1) for item in match.group(2).split())
            rows.append((match.group(1), fields))
    return rows


def number(fields, key):
    try:
        return int(fields[key], 0)
    except (KeyError, ValueError):
        return None


def result(verdict, reasons, **extra):
    return dict(verdict=verdict, reasons=list(reasons), **extra)


def split_boots(rows):
    boots = []
    for name, fields in rows:
        if name == 'M5D_BOOT_V1':
            boots.append((fields, []))
        elif boots:
            boots[-1][1].append((name, fields))
    return boots


def check_arm(fields, body, reasons):
    """Return (failed, feeds, complete) for one arm boot."""
    failed = False
    if number(fields, 'cause_status') != 0:
        reasons.append('arm boot reset cause unreadable')
    starts = [f for n, f in body if n == 'M5D_WDT_START_V1']
    feeds = [f for n, f in body if n == 'M5D_FEED_V1']
    stops = [f for n, f in body if n == 'M5D_FEED_STOP_V1']
    initial = [f for n, f in body if n == 'M5D_STATUS_V1' and f.get('stage') == 'initial']
    if initial and (number(initial[0], 'wd_status') != 0 or number(initial[0], 'wd_state') != IDLE):
        reasons.append('watchdog not idle before start')
        failed = True
    if len(starts) > 1:
        reasons.append('watchdog started more than once in one boot')
        failed = True
    if starts and (number(starts[0], 'status') != 0 or number(starts[0], 'timeout_ms') != START_TIMEOUT_MS
                   or number(starts[0], 'state') != ACTIVE):
        reasons.append('watchdog start failed or wrong timeout/state')
        failed = True
    if any(number(f, 'status') != 0 for f in feeds):
        reasons.append('feed returned an error')
        failed = True
    indexes = [number(f, 'index') for f in feeds]
    if stops and indexes != list(range(1, FEED_COUNT + 1)):
        reasons.append('feed records are not exactly 1..%d' % FEED_COUNT)
        failed = True
    if stops and number(stops[0], 'fed') != FEED_COUNT:
        reasons.append('feed stop reports wrong feed count')
        failed = True
    positions = {name: [i for i, (n, _) in enumerate(body) if n == name]
                 for name in ('M5D_WDT_START_V1', 'M5D_FEED_V1', 'M5D_FEED_STOP_V1')}
    if starts and feeds and min(positions['M5D_FEED_V1']) < positions['M5D_WDT_START_V1'][0]:
        reasons.append('feed before watchdog start')
        failed = True
    if stops and feeds and max(positions['M5D_FEED_V1']) > positions['M5D_FEED_STOP_V1'][0]:
        reasons.append('feed after the deliberate feed stop')
        failed = True
    complete = bool(starts and stops and initial)
    return failed, len(feeds), complete


def evaluate(data):
    rows = records(data)
    boots = split_boots(rows)
    reasons = []
    if not boots:
        return result('INCONCLUSIVE', ['no M5D boot record'], feeds=0)
    phases = [f.get('phase') for f, _ in boots]
    arm_boots = [b for b in boots if b[0].get('phase') == 'arm']
    report_boots = [b for b in boots if b[0].get('phase') == 'report']
    feeds = 0
    failed = False
    if len(arm_boots) > 1:
        return result('FAIL', ['more than one arm boot: repeated watchdog activation'],
                      feeds=feeds, phases=phases)
    if not arm_boots:
        return result('INCONCLUSIVE', ['no arm boot observed'], feeds=0, phases=phases)
    if phases[0] != 'arm':
        reasons.append('arm boot is not first')
        failed = True
    arm_failed, feeds, arm_complete = check_arm(arm_boots[0][0], arm_boots[0][1], reasons)
    failed = failed or arm_failed
    if len(report_boots) > 1:
        reasons.append('more than one report boot')
        failed = True
    if failed:
        return result('FAIL', reasons, feeds=feeds, phases=phases)
    arm_fields, arm_body = arm_boots[0]
    if (number(arm_fields, 'causes') or 0) & WATCHDOG_BIT:
        return result('INCONCLUSIVE', reasons + ['WATCHDOG cause already latched at the arm boot'],
                      feeds=feeds, phases=phases)
    if arm_fields.get('marker') != 'none':
        return result('INCONCLUSIVE', reasons + ['arm boot saw a pre-existing marker'],
                      feeds=feeds, phases=phases)
    if not arm_complete:
        return result('INCONCLUSIVE', reasons + ['arm boot record incomplete'], feeds=feeds, phases=phases)
    if any(n == 'M5D_NO_RESET_V1' for n, _ in arm_body):
        return result('INCONCLUSIVE', reasons + ['watchdog did not reset the CPU within the wait window'],
                      feeds=feeds, phases=phases)
    if not report_boots:
        return result('INCONCLUSIVE', reasons + ['no post-reset report boot observed'],
                      feeds=feeds, phases=phases)
    fields, body = report_boots[0]
    if any(n in ('M5D_WDT_START_V1', 'M5D_FEED_V1') for n, _ in body):
        return result('FAIL', reasons + ['watchdog start or feed in the report boot'],
                      feeds=feeds, phases=phases)
    if number(fields, 'cause_status') != 0:
        return result('INCONCLUSIVE', reasons + ['post-reset cause unreadable'], feeds=feeds, phases=phases)
    if fields.get('marker') != 'wd_armed':
        reasons.append('report boot did not see the armed marker')
        return result('INCONCLUSIVE', reasons, feeds=feeds, phases=phases)
    post = [f for n, f in body if n == 'M5D_STATUS_V1' and f.get('stage') == 'post_reset']
    if not post:
        return result('INCONCLUSIVE', reasons + ['post-reset status record missing'],
                      feeds=feeds, phases=phases)
    if number(post[0], 'wd_status') != 0 or number(post[0], 'wd_state') != IDLE:
        return result('FAIL', reasons + ['watchdog not idle after reset: it was restarted'],
                      feeds=feeds, phases=phases)
    supported = number(fields, 'supported') or 0
    causes = number(fields, 'causes') or 0
    claims = [f for n, f in body if n == 'M5D_RESULT_V1']
    if not (supported & WATCHDOG_BIT):
        return result('INCONCLUSIVE', reasons + ['WATCHDOG cause flag not reported as supported'],
                      feeds=feeds, phases=phases, causes=causes)
    if not (causes & WATCHDOG_BIT):
        return result('INCONCLUSIVE', reasons + ['WATCHDOG cause bit not observed'],
                      feeds=feeds, phases=phases, causes=causes)
    if not claims or claims[-1].get('result') != 'PASS':
        return result('INCONCLUSIVE', reasons + ['report result line missing or not PASS'],
                      feeds=feeds, phases=phases, causes=causes)
    return result('PASS', reasons, feeds=feeds, phases=phases, causes=causes)


if __name__ == '__main__':
    verdict = evaluate(Path(sys.argv[1]).read_bytes())
    print(verdict)
    sys.exit(0 if verdict['verdict'] == 'PASS' else 1)
