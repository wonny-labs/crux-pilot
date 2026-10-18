"""Receive-timed oracle for the bounded independent M3 watchdog scenario."""
import re


def require(condition, message):
    if not condition:
        raise ValueError(message)


def validate(lines, source):
    """Rows are (host monotonic receive seconds, complete unmodified line)."""
    require(lines, 'Empty capture')
    require(all(a[0] <= b[0] for a, b in zip(lines, lines[1:])), 'Receive clock reversed')
    text = '\n'.join(line for _, line in lines)
    require(not re.search(r'M3_\w*(?:FAILED|REJECTED)|FATAL ERROR|ASSERTION FAIL', text),
            'Runtime error')
    boots = [(i, t) for i, (t, s) in enumerate(lines) if s.startswith('M3_BOOT ')]
    require(len(boots) == 2, 'Expected exactly two identified boots')
    for i, _ in boots:
        require(lines[i][1] == f'M3_BOOT source={source} profile=independent-health commands=1 fault_default=off',
                'Wrong firmware or unsafe boot default')
    first, second = boots[0][0], boots[1][0]
    require(lines[0][1].startswith('*** Booting Zephyr'), 'Stale capture prefix')
    native_boots = [i for i, (_, s) in enumerate(lines) if s.startswith('*** Booting Zephyr')]
    require(len(native_boots) == 2 and native_boots[0] < first < native_boots[1] < second,
            'Native/source boot ordering mismatch')
    require(all(0 <= lines[source_i][0] - lines[native_i][0] <= .5
                for source_i, native_i in zip((first, second), native_boots)),
            'Buffered or mismatched native/source boot')
    require(text.count('M3_WDT_ARMED timeout_ms=2000 debug_pause=1 freshness_ms=500') == 1,
            'Missing/duplicate watchdog arm')
    arm = next(i for i, (_, s) in enumerate(lines) if s.startswith('M3_WDT_ARMED '))
    stalls = [(i, t, re.fullmatch(r'M3_FAULT_STALL monotonic_ms=(\d+) sequence=(\d+)', s))
              for i, (t, s) in enumerate(lines) if s.startswith('M3_FAULT_STALL ')]
    require(len(stalls) == 1 and stalls[0][2], 'Missing/duplicate critical stall')
    stall, stall_rx, match = stalls[0]
    stall_ms = int(match[1])
    require(first < arm < stall < second, 'Incorrect trial order')
    require(stall_rx - lines[arm][0] >= 12, 'Insufficient healthy watchdog interval')
    require(lines[-1][0] - lines[0][0] <= 30, 'Capture bound exceeded')
    reset_reports = [(i, s) for i, (_, s) in enumerate(lines) if s.startswith('M3_RESET ')]
    require(len(reset_reports) == 2 and first < reset_reports[0][0] < arm and second < reset_reports[1][0],
            'Missing or misplaced reset cause')
    for _, report in reset_reports:
        require(re.fullmatch(r'M3_RESET support_status=0 supported=0x[0-9a-f]+ cause_status=0 cause=0x[0-9a-f]+ clear_status=0 remaining_status=0 remaining=0x0', report),
                'Initial/final reset cause was not successfully read and cleared')
    work = []
    for i, (received, s) in enumerate(lines):
        if s.startswith('M3_WORK '):
            m = re.fullmatch(r'M3_WORK sequence=(\d+) monotonic_ms=(\d+) time=(-?\d+) pressed=([01]) device=0', s)
            require(m and int(m[3]) in (0, -4), 'Failed/malformed critical work')
            if first < i < stall:
                work.append((i, received, int(m[1]), int(m[2])))
    require(len(work) >= 14, 'Missing sustained actual critical work')
    require(work[-1][3] - work[0][3] >= 13000, 'Insufficient target work duration')
    for a, b in zip(work, work[1:]):
        require(0 < b[2] - a[2] <= 15 and 0 < b[3] - a[3] <= 1500,
                'Critical work stopped during healthy interval')
        require(abs((b[1] - a[1]) - (b[3] - a[3]) / 1000) <= .25,
                'Buffered/discontinuous work observations')
    feeds = []
    for i, (received, s) in enumerate(lines):
        if s.startswith('M3_WDT_FEED '):
            m = re.fullmatch(r'M3_WDT_FEED monotonic_ms=(\d+) sequence=(\d+) completed_ms=(\d+)', s)
            require(m and arm < i < second, 'Unexpected/malformed feed')
            now, seq, completed = map(int, m.groups())
            require(0 <= now - completed <= 500, 'Stale/future completion fed')
            require(i < stall or now - stall_ms <= 750, 'Late feed after critical stall')
            if feeds:
                require(now > feeds[-1][2] and seq >= feeds[-1][3] and completed >= feeds[-1][4],
                        'Feed progress reversed')
                require(now - feeds[-1][2] <= 250, 'Healthy feed gap')
                require(received - feeds[-1][1] <= 0.5, 'Discontinuous/buffered feed observations')
                require(abs((received - feeds[-1][1]) - (now - feeds[-1][2]) / 1000) <= .15,
                        'Feed receive/target timing mismatch')
            feeds.append((i, received, now, seq, completed))
    require(feeds and feeds[-1][0] > stall, 'Missing observed freshness expiry after stall')
    require(stall_ms - feeds[0][2] >= 12000, 'Insufficient healthy interval before actual stall')
    require(1 <= boots[1][1] - feeds[-1][1] <= 5, 'Reset timing outside declared envelope')
    require(any('M3_HEALTH_STALE ' in s for _, s in lines[stall:second]), 'No stale state')
    observers = []
    for i, (received, s) in enumerate(lines):
        if s.startswith('M3_OBSERVER_ALIVE '):
            m = re.fullmatch(r'M3_OBSERVER_ALIVE monotonic_ms=(\d+) healthy=([01]) sequence=(\d+) readiness=(\d+)', s)
            require(m and m[4] == '0', 'Missing/premature overall readiness')
            observers.append((i, received, *map(int, m.groups())))
    before_arm = [row for row in observers if first < row[0] < arm and row[3] == 1]
    require(len(before_arm) >= 2 and before_arm[-1][2] - before_arm[0][2] >= 1000,
            'No continuous healthy pre-arm evidence')
    require(all(row[3] == 1 for row in observers if before_arm[0][0] <= row[0] < stall),
            'Unhealthy observation before deliberate stall')
    require(any(stall < row[0] < second and row[3] == 0 for row in observers),
            'Observer not alive after critical work became stale')
    require(not any(s.startswith('M3_WORK ') for _, s in lines[stall:second]),
            'Critical work continued after stall')
    post = [row for row in observers if row[0] > second and row[3] == 1]
    require(len(post) >= 4 and post[-1][2] - post[0][2] >= 3000, 'Insufficient healthy post-reset observation')
    require(post[-1][4] > post[0][4], 'No post-reset workload progress')
    require(all(row[3] == 1 for row in observers if row[0] >= post[0][0]),
            'Unhealthy observation during recovery')
    reset = [s for _, s in lines[second:] if s.startswith('M3_RESET ')]
    require(len(reset) == 1, 'Missing/duplicate second reset cause')
    m = re.fullmatch(r'M3_RESET support_status=0 supported=0x([0-9a-f]+) cause_status=0 cause=0x([0-9a-f]+) clear_status=0 remaining_status=0 remaining=0x0', reset[0])
    require(m and int(m[1], 16) & 0x10 and int(m[2], 16) & 0x10,
            'Watchdog reset cause unavailable/uncleared')
    stacks = validate_stacks(lines)
    return {'status': 'PASS', 'scope': 'independent health only; full M3 remains Pending',
            'last_feed_to_boot_receive_s': boots[1][1] - feeds[-1][1],
            'worker_min_unused': min(s[0] for s in stacks),
            'main_min_unused': min(s[1] for s in stacks)}


def validate_stacks(lines):
    stacks = []
    observer_indices = [i for i, (_, s) in enumerate(lines) if s.startswith('M3_OBSERVER_ALIVE ')]
    stack_indices = [i for i, (_, s) in enumerate(lines) if s.startswith('M3_STACK ')]
    require(len(observer_indices) == len(stack_indices) and observer_indices, 'Incomplete observer/stack pairs')
    for n, index in enumerate(observer_indices):
        upper = observer_indices[n + 1] if n + 1 < len(observer_indices) else len(lines)
        require(index < stack_indices[n] < upper, 'Misordered stack observation')
    for _, s in lines:
        if s.startswith('M3_STACK '):
            m = re.fullmatch(r'M3_STACK worker_status=0 worker_object=4160 worker_usable=4096 worker_unused=(\d+) main_status=0 main_usable=2048 main_unused=(\d+)', s)
            require(m, 'Invalid stack accounting')
            worker, main = map(int, m.groups())
            require(1024 <= worker <= 4096 and 512 <= main <= 2048, 'Predeclared stack margin failed')
            stacks.append((worker, main))
    require(stacks, 'No stack evidence')
    return stacks
