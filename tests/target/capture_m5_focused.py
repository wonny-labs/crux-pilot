"""Focused one-boot M5 network capture (Group C, #28): one fresh boot, automatic host link loss.

Programming, restore and the board power cycle stay separate operator/LinkServer steps; this tool
only reads the DAPLink VCOM, writes runner commands, runs the peer and switches the dedicated host
NIC en5 down/up with `sudo -n`. It never resets the board and never reads a password.
"""
import argparse
import json
from pathlib import Path
import signal
import m3_clock as shared_clock
import capture_m5_network as runner
import m5_series
from m5_focused_adapter import FocusedAdapter, HostLink, RawPort

ABI_KEYS = ('ETIMEDOUT', 'ECONNRESET', 'ENETDOWN', 'ENETUNREACH')
RESET_OPERATION = 'focused_power_cycle_1'
RESET_WITNESS = m5_series.FOCUSED['reset_witness']


def load_abi(path):
    abi = json.loads(Path(path).read_text())
    runner.oracle.require(set(abi) >= set(ABI_KEYS) and all(type(abi[key]) is int and abi[key] > 0 for key in ABI_KEYS), 'ABI errno file must define positive integers for ' + ', '.join(ABI_KEYS))
    return {key: abi[key] for key in ABI_KEYS}


def write_index(evidence, nonce, source, boot_at):
    """Describe the retained raw files for replay_bundle; the power cycle is operator-driven.

    The host does not timestamp the power cycle itself: the boot start is the host receive time
    of the first M5_BOOT record (minus 1 ms), which is the only reset-like fact the host observed.
    """
    index = dict(
        schema=1, source=source, clock=m5_series.CLOCK,
        reset_semantics=RESET_WITNESS,
        boots=[dict(nonce=nonce, reset=dict(operation=RESET_OPERATION, monotonic=boot_at - .001), uart_directory='.',
                    writes_file='writes.jsonl', controls_file='controls.jsonl', peer_stops_file='peer-stops.jsonl',
                    peer_files={str(phase): 'peer-' + str(phase) + '.jsonl' for phase, count, _ in m5_series.FOCUSED['phases'] if count})])
    (Path(evidence) / 'index.json').write_text(json.dumps(index, indent=2) + '\n')


def capture(nonce, source, abi, evidence, uart, wait, *, link=None, port_factory=None, adapter_cls=FocusedAdapter,
            clock=shared_clock.monotonic, sleep=shared_clock.sleep, say=print, run=runner.run_boot, **adapter_kwargs):
    """One approved boot. The operator power-cycles the board after the capture is armed."""
    link = link or HostLink()
    # Fail closed before any UART access: a non-interactive sudo credential and the NIC identity.
    link.command(['sudo', '-n', 'true'])
    link.identity()
    evidence = Path(evidence)
    runner.oracle.require(not evidence.exists(), 'Never overwrite capture evidence')
    evidence.parent.mkdir(parents=True, exist_ok=True)
    adapter = adapter_cls(nonce, evidence, port_factory or (lambda: RawPort(uart)), link=link, clock=clock, **adapter_kwargs)
    primary = None
    try:
        say('Capture armed on ' + str(uart) + '. Power-cycle the board now (window ' + str(int(wait)) + ' s).')
        first = adapter.wait_for_boot(deadline=clock() + wait, sleep=sleep)
        runner.oracle.require(first is not None, 'No target boot banner before the operator deadline')
        # The cached sudo credential may have expired while waiting for the power cycle.
        link.command(['sudo', '-n', 'true'])
        link.configure()
        supervisor = runner.Supervisor(nonce, source, clock(), abi, profile=m5_series.FOCUSED)
        result = run(supervisor, adapter, clock)
        adapter.emit('result.jsonl', result)
        write_index(evidence, nonce, source, first['monotonic'])
        return result
    except BaseException as error:
        primary = error
        adapter.emit('result.jsonl', dict(status='ABORT_INCONCLUSIVE', error=repr(error), target_may_still_execute=True, automatic_reset=False))
        raise
    finally:
        problems = []
        if adapter.link_down:
            try:
                adapter.emit('abort-restore.jsonl', dict(event='restore_link', rows=adapter.link.up()))
            except BaseException as error:
                problems.append(repr(error))
        try:
            adapter.close()
        except BaseException as error:
            problems.append(repr(error))
        if problems and primary is not None:
            primary.evidence_failure = problems
        elif problems:
            raise ValueError('Cleanup failed: ' + '; '.join(problems))


def terminate(signum, frame):
    raise SystemExit(128 + signum)


def install_signal_handlers():
    """Closing the terminal or SIGTERM must run the cleanup that restores en5."""
    for number in (signal.SIGTERM, signal.SIGHUP):
        signal.signal(number, terminate)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest='mode', required=True)
    p = sub.add_parser('capture', help='Approved one-boot capture; no programming, reset or power control')
    p.add_argument('--execute-hardware', action='store_true', required=True)
    for name in ('uart', 'source', 'abi', 'evidence'):
        p.add_argument('--' + name, required=True)
    p.add_argument('--nonce', type=int, required=True)
    p.add_argument('--wait-seconds', type=float, default=5400)
    p = sub.add_parser('replay', help='Replay a retained focused bundle')
    for name in ('index', 'source', 'abi'):
        p.add_argument('--' + name, required=True)
    args = parser.parse_args()
    if args.mode == 'capture':
        install_signal_handlers()
        result = capture(args.nonce, args.source, load_abi(args.abi), args.evidence, args.uart, args.wait_seconds)
    else:
        result = runner.replay_bundle(args.index, args.source, load_abi(args.abi), m5_series.FOCUSED)
    print(json.dumps(result, indent=2, sort_keys=True))


if __name__ == '__main__':
    main()
