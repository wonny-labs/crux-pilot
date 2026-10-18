"""Verify actual native log-core counts under a controlled serial-delay model.

Synthetic workload payloads and the extra backend do not qualify Cortex timing,
stack margins, controller health or watchdog behaviour.
"""
import argparse
import importlib.util
import json
import re
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / 'target'))
from m3_integrated_output import exact, initial_idle_queue_samples, validate_grammar
spec = importlib.util.spec_from_file_location('integrated_fixture_formats',
    Path(__file__).resolve().parents[1] / 'integrated_output/verify_output.py')
fixture = importlib.util.module_from_spec(spec)
spec.loader.exec_module(fixture)
formats, render = fixture.formats, fixture.render

CASES = ('crux.native.logger_phase', 'crux.native.logger_phase_unshifted')
DRIFT = (0, 1, 2, 4)


def require(condition, message):
    if not condition:
        raise ValueError(message)


def verify_case(folder, raw=None):
    unshifted = folder.name.endswith('_unshifted')
    header = (folder / 'production_phase.h').read_text()
    require(re.fullmatch(r'#define CRUX_M3_INITIAL_OBSERVER_PHASE_MS 50\n', header) is not None,
            'Unreviewed production phase')
    phase = 0 if unshifted else 50
    config = set((folder / 'zephyr/.config').read_text().splitlines())
    required = {'CONFIG_BOARD_NATIVE_SIM=y', 'CONFIG_LOG_PRINTK=y', 'CONFIG_LOG_MODE_DEFERRED=y',
                'CONFIG_LOG_BACKEND_UART=y', 'CONFIG_LOG_PROCESS_THREAD=y',
                'CONFIG_LOG_PROCESS_THREAD_PRIORITY=14', 'CONFIG_MAIN_THREAD_PRIORITY=0',
                'CONFIG_LOG_PROCESS_THREAD_STACK_SIZE=2048', 'CONFIG_LOG_BUFFER_SIZE=16384',
                'CONFIG_SYS_CLOCK_TICKS_PER_SEC=10000'}
    forbidden = {'CONFIG_LOG_BLOCK_IN_THREAD=y', 'CONFIG_LOG_MODE_OVERFLOW=y',
                 'CONFIG_PRINTK_SYNC=y', 'CONFIG_LOG_BACKEND_NATIVE_POSIX=y',
                 'CONFIG_LOG_BACKEND_UART_ASYNC=y'}
    require(required <= config and not forbidden & config, 'Native configuration changed')
    raw = (folder / 'handler.log').read_bytes() if raw is None else raw
    lines = [s.removesuffix('\r') for s in raw.decode('ascii').split('\n')]
    require(not any('\x1b' in s or 'dropped' in s.lower() or 'FAILED' in s for s in lines),
            'Loss, ANSI or fixture failure')
    require(lines.count('PHASE_HELD_MESSAGE') == 1 and
            lines.count('PHASE_HOLD in_flight=1 completed=0 timeout=0') == 1 and
            lines.count(f'PHASE_MODEL phase_ms={phase} baud=115200 bits=10 worker_priority=5 '
                        'network_priority=6 main_priority=0 logger_priority=14') == 1 and
            lines.count(f'PHASE_COMPLETE cycles=4 unshifted={int(unshifted)}') == 1,
            'Missing/duplicate/incorrect mechanism or completion')
    marker_lines = [s for s in lines if 'PHASE_' in s]
    analyser = [f'PHASE_ANALYSER thread={n} STACK: unused 1024 usage 3072 / 4096 (75 %); CPU: 0 %'
                for n in range(12)]
    require(marker_lines[0] == 'PHASE_HELD_MESSAGE' and marker_lines[1].startswith('PHASE_HOLD ')
            and marker_lines[2].startswith('PHASE_MODEL ') and
            marker_lines[3:-1] == analyser * 2 and marker_lines[-1].startswith('PHASE_COMPLETE '),
            'Malformed, reordered or missing fixture records')
    payload = [s for s in lines if 'M3_' in s]
    validate_grammar(list(enumerate(payload)))
    fmt = formats(folder / 'production_formats.h')
    workers = [s for s in payload if s.startswith('M3_WORK ')]
    networks = [s for s in payload if s.startswith('M3_NET_')]
    main = [s for s in payload if s.startswith(('M3_OBSERVER_ALIVE ', 'M3_STACK ', 'M3_LOGGER '))]
    require(len(payload) == 24 and len(workers) == 4 and len(networks) == 8 and len(main) == 12,
            'Missing, duplicate or unexpected production records')
    pre_report_bytes = []
    snapshots = []
    for n in range(4):
        work = exact(workers[n], 'M3_WORK')
        require(n * 1000 <= work['monotonic_ms'] <= n * 1000 + 2 and workers[n] == render(
            fmt['WORK'], (1 + n * 10, work['monotonic_ms'], 0, 0, 0)), 'Worker rate/content changed')
        net_match = re.fullmatch(r'M3_NET_RESOURCE monotonic_ms=(\d+) owned=0 contexts=0 '
                        r'rx_pkt_free=16 tx_pkt_free=16 rx_buf_free=32 tx_buf_free=32',
                        networks[n * 2])
        require(net_match is not None, 'Malformed network resource')
        net_stamp = int(net_match[1])
        require(1 + n * 1001 <= net_stamp <= 3 + n * 1001 and
                networks[n * 2] == render(fmt['NET_RESOURCE'], (net_stamp, 0, 0, 16, 16, 32, 32)) and
                networks[n * 2 + 1] == render(fmt['NET_STATE'], (net_stamp, 0, 1, 0)),
                'Network idle rate/content changed')
        observer, stack, logger = main[n * 3:n * 3 + 3]
        obs = exact(observer, 'M3_OBSERVER_ALIVE')
        stamp = obs['monotonic_ms']
        require(n * 1000 + phase + DRIFT[n] <= stamp <= n * 1000 + phase + DRIFT[n] + 2 and
                observer == render(fmt['OBSERVER_ALIVE'],
                    (stamp, 0, 1 + n * 10, 0, 0, 0, 0, 0, 0)) and stack == render(fmt['STACK'],
                    (0, 4160, 4096, 1024, 0, 4096, 1024, 0, 4160, 4096, 1024)),
                'Fixed sample phase/order/content changed')
        resource = exact(logger, 'M3_LOGGER')
        require(resource['monotonic_ms'] == resource['queue_snapshot_ms'] == stamp and
                resource['found'] == 1 and resource['status'] == 0 and resource['priority'] == 14 and
                resource['configured'] == resource['usable'] == 2048 and
                0 <= resource['unused'] <= 2048 and 0 <= resource['queued'] <= 16384,
                'Invalid native logger resource/count')
        snapshots.append(dict(monotonic_ms=stamp, queued=resource['queued']))
        # Actual production-format records, plus CR for the UART's CRLF encoding.
        pre_report_bytes.append(sum(len(s) + 2 for s in
                                    (workers[n], networks[n * 2], networks[n * 2 + 1])))
    policy_pass = True
    try:
        initial_idle_queue_samples(snapshots)
    except ValueError:
        policy_pass = False
    require(policy_pass != unshifted, 'Fixed-phase pass / original-phase rejection not reproduced')
    if unshifted:
        require(any(s['queued'] > 0 for s in snapshots[1:]), 'Unshifted has no in-flight observation')
    bounded_burst = [render(fmt['WORK'], (901, 90000, 0, 0, 0)),
                     render(fmt['NET_RESOURCE'], (90000, 0, 0, 16, 16, 32, 32)),
                     render(fmt['NET_STATE'], (90000, 1, 1, 0))]
    bounded_bytes = sum(len(s) + 2 for s in bounded_burst)
    require(max(max(pre_report_bytes), bounded_bytes) * 10 * 1000 / 115200 < 50,
            'Pre-report byte budget exceeds phase')
    # Preserve global boundaries in addition to each producer's complete sequence.
    require(all(lines.index(marker_lines[2]) < lines.index(s) < lines.index(marker_lines[-1])
                for s in payload), 'Work outside declared model phase')
    return dict(status='EXPECTED_ORIGINAL_PHASE_REJECTION' if unshifted else 'FIRST_THREE_ZERO',
                samples=snapshots, pre_report_bytes=pre_report_bytes,
                bounded_idle_90s_bytes=bounded_bytes,
                bounded_idle_90s_serial_ms=round(bounded_bytes * 10 * 1000 / 115200, 3),
                pre_report_serial_ms=[round(b * 10 * 1000 / 115200, 3) for b in pre_report_bytes],
                scope='native kernel + controlled delay model; no physical timing/stack qualification')


def negative_controls(folder):
    raw = (folder / 'handler.log').read_bytes()
    rows = raw.split(b'\n')
    sample = next(s for s in rows if s.startswith(b'M3_LOGGER monotonic_ms=105'))
    observer = next(s for s in rows if s.startswith(b'M3_OBSERVER_ALIVE monotonic_ms=105'))
    mutations = [raw.replace(sample, sample.replace(b'queued=0', b'queued=1'), 1),
                 raw.replace(sample + b'\n', b'', 1),
                 raw.replace(sample, sample + b'\n' + sample, 1),
                 raw.replace(sample, b'\x1b[0m' + sample, 1),
                 raw.replace(sample, sample[:len(sample)//2], 1),
                 raw.replace(observer, re.sub(rb'monotonic_ms=\d+', b'monotonic_ms=6051', observer), 1),
                 raw + b'--- 1 messages dropped ---\n']
    # Reorder a real group, retaining every byte of its records.
    index = rows.index(sample)
    changed = list(rows)
    changed[index - 1], changed[index] = changed[index], changed[index - 1]
    mutations.append(b'\n'.join(changed))
    for changed in mutations:
        require(changed != raw, 'Ineffective negative mutation')
        try:
            verify_case(folder, changed)
        except (ValueError, AttributeError):
            pass
        else:
            raise ValueError('Corrupt or favourable-resampled actual stream accepted')
    return len(mutations)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('directory', type=Path)
    root = parser.parse_args().directory
    found = {p.parent.name: p.parent for p in root.rglob('handler.log') if p.parent.name in CASES}
    require(set(found) == set(CASES), 'Missing native phase cases')
    result = {name: verify_case(found[name]) for name in CASES}
    result['negative_controls'] = negative_controls(found[CASES[0]])
    print(json.dumps(result, indent=2))
