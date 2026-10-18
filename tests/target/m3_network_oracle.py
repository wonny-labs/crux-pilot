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


def checkpoint(rows, after, ready, link=1, *, native_output=False):
    if native_output:
        from m3_network_output import validate_native_records, logger_checkpoint
        validate_native_records(rows)
        logger_checkpoint(rows, after)
    resources = [(i, records([(t, line)], 'M3_NET_RESOURCE')[0])
                 for i, (t, line) in enumerate(rows) if 'M3_NET_RESOURCE ' in line]
    idle = [(i, r) for i, r in resources if after <= r['received'] <= after + 5][:3]
    require(len(idle) == 3, 'Missing three consecutive idle resource reports')
    require(idle[-1][1]['monotonic_ms'] - idle[0][1]['monotonic_ms'] >= 1800 and
            all(750 <= b[1]['monotonic_ms'] - a[1]['monotonic_ms'] <= 1250
                for a, b in zip(idle, idle[1:])), 'Resource reports are not consecutive idle observations')
    require(not any(t >= after and ('M3_NET_STATE ' in line or 'M3_NET_STACK ' in line)
                    for t, line in rows[:idle[0][0]]), 'Orphan checkpoint state/stack before its resource report')
    unused = []
    for index, resource in idle:
        require([resource[k] for k in ('owned', 'contexts', 'rx_pkt_free', 'tx_pkt_free',
                                      'rx_buf_free', 'tx_buf_free')] == [0, 0, 16, 16, 32, 32],
                'Native resources did not return to immutable full baseline')
        upper = next((i for i, _ in resources if i > index), len(rows))
        section = rows[index+1:upper]
        states = records(section, 'M3_NET_STATE')
        stacks = records(section, 'M3_NET_STACK')
        require(len(states) == len(stacks) == 1, 'Missing/duplicate paired idle state or stack')
        state, stack = states[0], stacks[0]
        state_index = next(i for i, (_, line) in enumerate(section) if 'M3_NET_STATE ' in line)
        stack_index = next(i for i, (_, line) in enumerate(section) if 'M3_NET_STACK ' in line)
        require(state_index < stack_index and all(
            0 <= item['received'] - resource['received'] <= .25 and item['received'] <= after + 5
            for item in (state, stack)), 'Idle state/stack ordering or observation bound failed')
        require(0 <= state['monotonic_ms'] - resource['monotonic_ms'] <= 250 and
                state['link'] == link and state['address'] == 1 and state['service_ready'] == ready,
                'Paired idle readiness/address/link mismatch')
        require(stack['status'] == 0 and stack['usable'] == 4096 and 1024 <= stack['unused'] <= 4096,
                'Paired stack checkpoint failed')
        unused.append(stack['unused'])
    return dict(samples=3, span_ms=idle[-1][1]['monotonic_ms']-idle[0][1]['monotonic_ms'],
                min_unused=min(unused))


def validate_stage(rows, peer, trial, mode, link_down=False, *, errno_abi, native_output=False, native_capture=None):
    if native_output:
        from m3_network_output import validate_native_records, logger_pairs
        capture = rows if native_capture is None else native_capture
        validate_native_records(capture)
        logger_pairs(capture, allow_prefix=True)
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
        resource_result = checkpoint(rows, summary['received'] + .25, ready, 0 if link_down else 1, native_output=native_output)
    if native_output and link_down and fenced:
        from m3_network_output import logger_checkpoint
        logger_checkpoint(rows, summary['received'] + .25)
    return dict(status='PASS', trial=trial, mode=mode, exchanges=len(exchanges),
                link_down=link_down, resources=resource_result)
