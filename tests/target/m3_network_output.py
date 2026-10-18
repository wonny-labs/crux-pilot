"""Extra finite native-logger observations; original Ethernet verdicts stay separate."""
import re
from m3_health_oracle import require
from m3_image import check_record_loss

FIELDS = {
    'RESOURCE': 'monotonic_ms owned contexts rx_pkt_free tx_pkt_free rx_buf_free tx_buf_free',
    'STATE': 'monotonic_ms link address service_ready',
    'STACK': 'status usable unused',
    'LOGGER': 'monotonic_ms queue_snapshot_ms found status configured usable unused priority queued',
    'EXCHANGE': 'trial protocol sequence result monotonic_ms elapsed_ms',
    'ROUND_BUDGET': 'trial started_ms deadline_ms',
    'ROUND': 'trial result monotonic_ms',
    'CLEANUP': 'result expected_contexts observed_contexts elapsed_ms restart_required',
    'BASELINE': 'contexts',
}


def exact_record(line, name):
    tokens = line.split(' ')
    expected = FIELDS[name.removeprefix('M3_NET_')].split()
    require(tokens[0] == name and len(tokens) == len(expected)+1, 'Malformed native record')
    pairs = [token.split('=') for token in tokens[1:]]
    require(all(len(pair) == 2 for pair in pairs) and [p[0] for p in pairs] == expected and
            all(re.fullmatch(r'-?[0-9]+', p[1]) for p in pairs), 'Invalid native record fields/order')
    return {key:int(value) for key,value in pairs}


def validate_native_records(rows):
    check_record_loss(rows)
    require(all(a[0] <= b[0] for a,b in zip(rows,rows[1:])), 'Receive timeline reversed')
    for _,line in rows:
        if 'M3_NET_' not in line:
            continue
        name = line.split(' ')[0]
        if name.removeprefix('M3_NET_') in FIELDS:
            exact_record(line,name)
        else:
            require(line == 'M3_NET_REQUESTED' or
                    re.fullmatch(r'M3_NET_BOOT source=[0-9a-f]{12} profile=network-probe exposure=isolated-only',line) or
                    line == 'M3_NET_INIT result=0 board=192.168.77.2 peer=192.168.77.1',
                    'Unexpected/malformed native network marker')
    require(not any('M3_MDIO_' in line or 'M3_OUTPUT' in line for _,line in rows),
            'Startup/PHY diagnostic output in qualifying profile')


def logger_pairs(rows, *, allow_prefix=False):
    resources = [i for i,(_,s) in enumerate(rows) if s.startswith('M3_NET_RESOURCE ')]
    require(resources, 'Missing native resource reports')
    first = resources[0]
    prefix = [s.split(' ')[0] for _,s in rows[:first]
              if s.startswith(('M3_NET_STATE ', 'M3_NET_STACK ', 'M3_NET_LOGGER '))]
    require(not prefix or (allow_prefix and prefix in
            [['M3_NET_LOGGER'], ['M3_NET_STACK','M3_NET_LOGGER'],
             ['M3_NET_STATE','M3_NET_STACK','M3_NET_LOGGER']]),
            'Invalid initial partial native resource group')
    for _,line in rows:
        if line.startswith('M3_NET_STACK '):
            stack = exact_record(line,'M3_NET_STACK')
            require(stack['status'] == 0 and stack['usable'] == 4096 and
                    1024 <= stack['unused'] <= 4096, 'Native main stack observation failed')
        if line.startswith('M3_NET_RESOURCE '):
            resource = exact_record(line,'M3_NET_RESOURCE')
            require(0 <= resource['owned'] <= 2 and 0 <= resource['contexts'] <= 4 and
                    all(0 <= resource[k] <= limit for k,limit in
                        [('rx_pkt_free',16),('tx_pkt_free',16),('rx_buf_free',32),('tx_buf_free',32)]),
                    'Native resource observation outside configured bounds')
    result = {}
    paired_stacks = set()
    for i,(received,line) in enumerate(rows):
        if not line.startswith('M3_NET_LOGGER '):
            continue
        logger = exact_record(line,'M3_NET_LOGGER')
        require(logger['found'] == 1 and logger['status'] == 0 and
                logger['configured'] == logger['usable'] == 2048 and
                1024 <= logger['unused'] <= 2048 and logger['priority'] == 14 and
                0 <= logger['queued'] <= 4096, 'Native logger resource observation failed')
        previous = [j for j,(_,s) in enumerate(rows[:i]) if s.startswith('M3_NET_RESOURCE ')]
        if not previous:
            require(allow_prefix and i < first, 'Orphan native logger')
            continue
        start = previous[-1]
        group = [(t,s) for t,s in rows[start:i+1] if s != 'M3_NET_REQUESTED']
        require([s.split(' ')[0] for _,s in group] ==
                ['M3_NET_RESOURCE','M3_NET_STATE','M3_NET_STACK','M3_NET_LOGGER'],
                'Native resource group is missing, duplicated or reordered')
        resource = exact_record(rows[start][1],'M3_NET_RESOURCE')
        require(all(0 <= t-rows[start][0] <= .25 for t,_ in group) and
                0 <= resource['monotonic_ms']-logger['queue_snapshot_ms'] <= 250 and
                0 <= logger['monotonic_ms']-resource['monotonic_ms'] <= 250,
                'Native logger resource/queue snapshot association exceeded')
        stack_index = [j for j in range(start,i) if rows[j][1].startswith('M3_NET_STACK ')]
        require(len(stack_index) == 1, 'Missing or duplicate native main stack')
        paired_stacks.add(stack_index[0])
        result[start] = logger
    require(paired_stacks == {i for i,(_,s) in enumerate(rows) if i >= first and s.startswith('M3_NET_STACK ')},
            'Periodic main stack has no complete logger observation')
    require(result, 'Missing native logger observations')
    return result


def logger_checkpoint(rows, after):
    pairs = logger_pairs(rows, allow_prefix=True)
    idle = [(i,t,exact_record(s,'M3_NET_RESOURCE')) for i,(t,s) in enumerate(rows)
            if after <= t <= after+5 and s.startswith('M3_NET_RESOURCE ')][:3]
    require(len(idle) == 3 and idle[-1][2]['monotonic_ms']-idle[0][2]['monotonic_ms'] >= 1800 and
            all(750 <= b[2]['monotonic_ms']-a[2]['monotonic_ms'] <= 1250 for a,b in zip(idle,idle[1:])),
            'Missing first consecutive bounded native idle observations')
    require(all(i in pairs and pairs[i]['queued'] == 0 for i,_,_ in idle),
            'First consecutive idle snapshots did not show a drained logger')
    return dict(samples=3,min_unused=min(pairs[i]['unused'] for i,_,_ in idle),queue=0)


def acknowledged_rows(rows, written):
    validate_native_records(rows)
    acknowledgements = [i for i,(t,s) in enumerate(rows) if s == 'M3_NET_REQUESTED' and t >= written]
    require(len(acknowledgements) == 1, 'Missing/duplicate exact command acknowledgement')
    index = acknowledgements[0]
    require(0 <= rows[index][0]-written <= 1 and not any(
        t >= written and s.startswith(('M3_NET_ROUND','M3_NET_EXCHANGE','M3_NET_CLEANUP'))
        for t,s in rows[:index]), 'Work published before acknowledgement or late acknowledgement')
    return rows[index:]
