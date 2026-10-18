"""Pure, replayable verdicts for the predeclared isolated Ethernet stages."""
import re
from m3_health_oracle import require


def records(rows, name):
    result = []
    for received, line in rows:
        start = line.find(name + ' ')
        if start < 0:
            continue
        text = line[start:]
        fields = {}
        for key, value in re.findall(r'(\w+)=([^ ]+)', text):
            fields[key] = int(value) if re.fullmatch(r'-?\d+', value) else value
        result.append(dict(fields, received=received))
    return result


def checkpoint(rows, after, ready, link=1):
    resources = records(rows, 'M3_NET_RESOURCE')
    idle = [r for r in resources if after <= r['received'] <= after + 5]
    require(len(idle) >= 3, 'Missing three idle resource reports')
    idle = idle[:3]
    require(idle[-1]['monotonic_ms'] - idle[0]['monotonic_ms'] >= 1800,
            'Resource reports are not consecutive idle observations')
    for r in idle:
        require([r[k] for k in ('owned', 'contexts', 'rx_pkt_free', 'tx_pkt_free',
                               'rx_buf_free', 'tx_buf_free')] == [0, 0, 16, 16, 32, 32],
                'Native resources did not return to immutable full baseline')
    states = [r for r in records(rows, 'M3_NET_STATE') if after <= r['received'] <= idle[-1]['received']]
    require(len(states) >= 3 and all(r['link'] == link and r['address'] == 1 and
                                   r['service_ready'] == ready for r in states),
            'Idle readiness/address/link mismatch')
    stacks = [r for r in records(rows, 'M3_NET_STACK') if after <= r['received'] <= idle[-1]['received']]
    require(len(stacks) >= 3 and all(r['status'] == 0 and r['usable'] == 4096 and
                                   r['unused'] >= 1024 for r in stacks), 'Stack checkpoint failed')
    return dict(samples=3, span_ms=idle[-1]['monotonic_ms'] - idle[0]['monotonic_ms'],
                min_unused=min(r['unused'] for r in stacks))


def validate_stage(rows, peer, trial, mode, link_down=False, *, errno_abi):
    require(mode in ('echo', 'silence', 'close'), 'Unknown stage mode')
    timeout = -errno_abi['ETIMEDOUT']
    reset = -errno_abi['ECONNRESET']
    io_error = -errno_abi['EIO']
    link_errors = {-errno_abi[name] for name in ('ENETDOWN', 'ENETUNREACH', 'ECONNRESET',
                   'ENOTCONN', 'ETIMEDOUT', 'ECONNREFUSED', 'EHOSTUNREACH')}
    require(not any(re.search(r'FATAL ERROR|ASSERTION FAIL|M3_NET_REJECTED|M3_NET_STOP', s)
                    for _, s in rows), 'Unexpected target diagnostic/reset lifecycle')
    require(not any(s.startswith('*** Booting Zephyr') for _, s in rows), 'Unexpected reset within stage')
    budgets = records(rows, 'M3_NET_ROUND_BUDGET')
    summaries = records(rows, 'M3_NET_ROUND')
    require(len(budgets) == len(summaries) == 1, 'Missing/duplicate round boundary')
    budget, summary = budgets[0], summaries[0]
    require(budget['trial'] == summary['trial'] == trial and
            budget['deadline_ms'] - budget['started_ms'] == 150000 and
            0 <= summary['monotonic_ms'] - budget['started_ms'] <= 150250,
            'Round identity/deadline mismatch')
    exchanges = records(rows, 'M3_NET_EXCHANGE')
    cleanup = records(rows, 'M3_NET_CLEANUP')
    require(cleanup and all(0 <= c['elapsed_ms'] <= 5250 for c in cleanup), 'Cleanup budget missing/exceeded')
    require(all((c['result'], c['restart_required']) in ((0, 0), (timeout, 1)) for c in cleanup),
            'Unexpected cleanup result/fence')
    require(all(c['expected_contexts'] == 0 and
                (c['observed_contexts'] == 0 if c['result'] == 0 else c['observed_contexts'] <= 1)
                for c in cleanup), 'Native cleanup baseline changed or exceeded')
    fenced = any(c['restart_required'] == 1 for c in cleanup)
    if fenced:
        fence = next(i for i, (_, line) in enumerate(rows)
                     if 'M3_NET_CLEANUP ' in line and 'restart_required=1' in line)
        require(not records(rows[fence+1:], 'M3_NET_EXCHANGE') and all(
            r['owned'] == 0 and r['contexts'] <= 1
            for r in records(rows[fence+1:], 'M3_NET_RESOURCE')), 'New work after hard cleanup fence')
    expected = [(p, s) for p in (1, 2) for s in range(1, 11)]
    actual = [(r['protocol'], r['sequence']) for r in exchanges]
    require(actual == (expected[:len(actual)] if link_down and fenced else expected),
            'Missing, duplicate or out-of-order transactions')
    require(exchanges and all(r['trial'] == trial and 0 <= r['elapsed_ms'] <= 2250
                             for r in exchanges), 'Exchange identity/time bound failed')
    previous_end = budget['started_ms']
    for r in exchanges:
        require(previous_end <= r['monotonic_ms'] - r['elapsed_ms'] and
                r['monotonic_ms'] <= min(summary['monotonic_ms'], budget['deadline_ms'] + 250),
                'Exchange timeline is backwards or outside round deadline')
        previous_end = r['monotonic_ms']
        expected_result = (0 if mode == 'echo' or (mode == 'close' and r['protocol'] == 2)
                           else timeout if mode == 'silence' else reset)
        require(r['result'] in link_errors if link_down
                else r['result'] == expected_result, 'Unexpected exchange result')
    ready = int(mode == 'echo' and not link_down)
    require(summary['result'] == (timeout if fenced else 0 if ready else io_error),
            'Unexpected aggregate result')
    require(link_down or not fenced, 'Unexpected cleanup fence')
    require(len([r for r in peer if r['event'] == 'peer_ready']) == 1 and
            len([r for r in peer if r['event'] == 'peer_closed']) == 1 and
            not any(r['event'] == 'rejected' for r in peer), 'Peer lifecycle/error')
    first, last = peer[0], peer[-1]
    require(first.get('clock') == 'clock_gettime(CLOCK_MONOTONIC)', 'Peer receive clock domain mismatch')
    require(first['event'] == 'peer_ready' and last['event'] == 'peer_closed' and
            first['monotonic'] <= budget['received'] <= summary['received'] <= last['monotonic'],
            'Peer was not live for the whole round')
    frames = [r for r in peer if r['event'] == 'exchange']
    require(last['exchanges'] == len(frames), 'Peer count mismatch')
    if link_down:
        require(not frames, 'Unexpected payload delivered during physical link loss')
    else:
        require([(1 if r['protocol'] == 'tcp' else 2, r['sequence']) for r in frames] == expected,
                'Peer transactions missing, duplicate or out of order')
        for r in frames:
            response = ('sent' if mode == 'echo' or (mode == 'close' and r['protocol'] == 'udp')
                        else 'closed' if mode == 'close' else 'withheld')
            require(r['trial'] == trial and r['mode'] == mode and r['received_bytes'] == 32 and
                    r['peer'][0] == '192.168.77.2' and r['response'] == response,
                    'Peer frame identity/response mismatch')
    states = records(rows, 'M3_NET_STATE')
    require(states and states[-1]['service_ready'] == ready, 'Final service readiness mismatch')
    require(states[0]['service_ready'] == 0 and all(
        not r['service_ready'] or r['monotonic_ms'] >= exchanges[-1]['monotonic_ms']
        for r in states), 'Premature service readiness')
    if link_down and fenced:
        resource = records(rows, 'M3_NET_RESOURCE')[-1]
        require(resource['owned'] == 0 and resource['contexts'] <= 1 and
                cleanup[-1]['restart_required'] == 1 and states[-1]['link'] == 0,
                'Unbounded link-loss ownership/fence')
        resource_result = dict(status='IN_PLACE_CLEANUP_FAILED', explicit_restart_required=True)
    else:
        resource_result = checkpoint(rows, summary['received'] + .25, ready, 0 if link_down else 1)
    return dict(status='PASS', trial=trial, mode=mode, exchanges=len(exchanges),
                link_down=link_down, resources=resource_result)
