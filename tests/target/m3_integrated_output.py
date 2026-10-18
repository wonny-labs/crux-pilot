"""Additional native-output evidence; existing integrated functional limits are unchanged."""
import re
from m3_health_oracle import require
from m3_image import check_record_loss
from m3_integrated_oracle import stacks
from m3_network_output import exact_record

FIELDS = {
    'M3_OBSERVER_ALIVE': 'monotonic_ms healthy sequence readiness network_sequence network_completed_ms network_active network_status window_end_ms',
    'M3_STACK': 'worker_status worker_object worker_usable worker_unused main_status main_usable main_unused network_status network_object network_usable network_unused',
    'M3_LOGGER': 'monotonic_ms queue_snapshot_ms found status configured usable unused priority queued',
    'M3_WORK': 'sequence monotonic_ms time pressed device',
    'M3_NET_WORK': 'sequence completed_ms result',
    'M3_NET_WINDOW_STOP': 'monotonic_ms result',
    'M3_NET_OWNER_INIT': 'result automatic_window',
    'M3_NET_WINDOW': 'started_ms end_ms',
    'M3_NET_WINDOW_REQUESTED': 'duration_ms',
    'M3_WDT_FEED': 'monotonic_ms sequence completed_ms network_sequence network_completed_ms',
    'M3_WDT_ARMED': 'timeout_ms debug_pause freshness_ms network_freshness_ms',
    'M3_STALL_REQUESTED': 'requested_ms deadline_ms',
    'M3_NET_STALL_REQUESTED': 'requested_ms deadline_ms',
    'M3_FAULT_STALL': 'monotonic_ms sequence requested_ms deadline_ms window_end_ms',
    'M3_NET_FAULT_STALL': 'monotonic_ms sequence completed_ms requested_ms deadline_ms window_end_ms',
    'M3_HEALTH_STALE': 'monotonic_ms sequence network_sequence network_completed_ms window_end_ms',
}
NETWORK_MARKERS = {'M3_NET_'+name for name in ('RESOURCE','STATE','EXCHANGE','CLEANUP','BASELINE')}
SPECIAL = (
    r'M3_BOOT source=[0-9a-f]{12} profile=integrated commands=[01] fault_default=off',
    r'M3_SCOPE non_uart=SW5 network=required (?:armed=[01]|readiness=not_ready automatic_window=0)',
    r'M3_INIT time=-?[0-9]+ device=-?[0-9]+ stimulus=SW5 network=required',
    r'M3_RESET support_status=-?[0-9]+ supported=0x[0-9a-f]+ cause_status=-?[0-9]+ cause=0x[0-9a-f]+ clear_status=-?[0-9]+ remaining_status=-?[0-9]+ remaining=0x[0-9a-f]+',
    r'M3_ARM_REQUESTED', r'M3_ARM_REJECTED (?:already_armed|not_healthy|attempted)',
    r'M3_(?:NET_)?STALL_REJECTED not_armed_or_repeated',
    r'M3_STALL_REJECTED checked_ms=[0-9]+ requested_ms=[0-9]+ deadline_ms=[0-9]+ window_end_ms=[0-9]+ remaining_ms=-?[0-9]+ reason=(?:expired|inactive_or_window_budget)',
    r'M3_NET_STALL_REJECTED checked_ms=[0-9]+ requested_ms=[0-9]+ deadline_ms=[0-9]+ window_end_ms=[0-9]+ remaining_ms=-?[0-9]+ reason=(?:expired|window_budget)',
    r'M3_NET_WINDOW_REJECTED repeated', r'M3_WDT_FAILED (?:setup|feed)=-?[0-9]+',
    r'M3_WORK_FAILED schedule',
)


def exact(line, name):
    fields = FIELDS[name].split()
    tokens = line.split(' ')
    require(tokens[0] == name and len(tokens) == len(fields)+1, 'Malformed integrated output')
    pairs = [token.split('=') for token in tokens[1:]]
    require(all(len(pair) == 2 for pair in pairs) and [p[0] for p in pairs] == fields and
            all(re.fullmatch(r'-?[0-9]+', p[1]) for p in pairs), 'Invalid integrated fields/order')
    result={key:int(value) for key,value in pairs}
    fixed={'M3_NET_OWNER_INIT':{'automatic_window':0}, 'M3_NET_WORK':{'result':0},
           'M3_NET_WINDOW_REQUESTED':{'duration_ms':60000}}
    require(all(result[key]==value for key,value in fixed.get(name,{}).items()),
            'Changed fixed integrated output fact')
    return result


def validate_grammar(rows):
    """Recognise every embedded required marker before permissive functional parsing."""
    check_record_loss(rows)
    require(not any('\x1b' in line for _,line in rows), 'ANSI output is not qualifying evidence')
    for _,line in rows:
        if 'M3_' not in line:continue
        name = line.split(' ')[0]
        if name in FIELDS:
            exact(line,name)
        elif name in NETWORK_MARKERS:
            exact_record(line, name)
        else:
            require(any(re.fullmatch(pattern,line) for pattern in SPECIAL),
                    'Unexpected/malformed integrated marker')


def logger_groups(rows):
    """Check every complete observer group across all explicit boot epochs."""
    validate_grammar(rows)
    require(all(a[0] <= b[0] for a,b in zip(rows,rows[1:])), 'Integrated receive clock reversed')
    observers = [i for i,(_,s) in enumerate(rows) if s.startswith('M3_OBSERVER_ALIVE ')]
    require(observers, 'No integrated native observer')
    paired = set()
    result = {}
    for n,start in enumerate(observers):
        upper = observers[n+1] if n+1 < len(observers) else len(rows)
        stack = [i for i in range(start+1,upper) if rows[i][1].startswith('M3_STACK ')]
        logger = [i for i in range(start+1,upper) if rows[i][1].startswith('M3_LOGGER ')]
        require(len(stack) == len(logger) == 1 and stack[0] < logger[0] and not any(
            rows[i][1].startswith('*** Booting Zephyr') for i in range(start+1,logger[0])),
            'Missing/duplicate/reordered or cross-reset logger group')
        value = exact(rows[logger[0]][1], 'M3_LOGGER')
        observer = exact(rows[start][1], 'M3_OBSERVER_ALIVE')
        require(value['found'] == 1 and value['status'] == 0 and
                value['configured'] == value['usable'] == 2048 and
                1024 <= value['unused'] <= 2048 and value['priority'] == 14 and
                0 <= value['queued'] <= 16384, 'Integrated logger resource observation failed')
        require(0 <= rows[logger[0]][0]-rows[start][0] <= .25 and
                0 <= value['queue_snapshot_ms']-observer['monotonic_ms'] <= 250 and
                0 <= value['monotonic_ms']-value['queue_snapshot_ms'] <= 250,
                'Integrated native logger association exceeded')
        paired.update((stack[0], logger[0]))
        result[start] = value
    require(paired == {i for i,(_,s) in enumerate(rows)
                       if s.startswith(('M3_STACK ', 'M3_LOGGER '))}, 'Orphan integrated stack/logger')
    stacks(rows)
    return result


def initial_idle_queue_samples(samples):
    """Select the original first three idle samples; shared by the native API probe."""
    initial = [sample for sample in samples if sample['monotonic_ms'] >= 1000][:3]
    require(len(initial) == 3 and initial[-1]['monotonic_ms'] <= 5000 and
            initial[-1]['monotonic_ms']-initial[0]['monotonic_ms'] >= 1800 and
            all(750 <= b['monotonic_ms']-a['monotonic_ms'] <= 1250
                for a,b in zip(initial,initial[1:])), 'Missing consecutive initial logger samples')
    require(all(sample['queued'] == 0 for sample in initial), 'Initial logger queue not drained')
    return initial


def idle_logger_checkpoint(rows):
    groups = logger_groups(rows)
    initial = initial_idle_queue_samples([
        dict(value,monotonic_ms=exact(rows[i][1], 'M3_OBSERVER_ALIVE')['monotonic_ms'])
        for i,value in groups.items()])
    return dict(samples=3, min_unused=min(sample['unused'] for sample in initial), queue=0)


def retained_prefix(rows, role):
    """Identify the prior role at a planned reset; never use its rows as new evidence."""
    if role == 'original':
        from capture_m3_network import validate_normal_prefix
        return validate_normal_prefix(rows)
    require(role in ('probe', 'normal'), 'Unknown preceding integrated role')
    for _,line in rows:
        if line == '' or line == 'Thread analyze:' or re.fullmatch(
                r'[ ]{0,64}(?:[A-Za-z0-9_./ -]{1,64}|rx_q\[0\][ ]{0,64}): STACK: unused \d+ usage \d+ / \d+ \(\d+ %\); CPU: \d+ %', line) or re.fullmatch(
                r'[ ]{0,64}: Total CPU cycles used: \d+', line):
            continue
        name = line.split(' ')[0]
        if name in {'M3_OBSERVER_ALIVE','M3_STACK','M3_LOGGER','M3_WORK',
                    'M3_NET_WORK','M3_NET_WINDOW_STOP'}:
            exact(line, name)
        elif name in NETWORK_MARKERS:
            exact_record(line, name)
        else:
            raise ValueError('Unexpected pre-reset integrated output')
    return None
