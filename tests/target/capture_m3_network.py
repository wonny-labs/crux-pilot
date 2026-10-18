"""Explicit seven-stage Ethernet qualification; exact reviewed images and isolated peer only."""
import argparse
import fcntl
import json
import os
import plistlib
from pathlib import Path
import re
import signal
import subprocess
import sys
import termios
import m3_clock as time
import traceback

from capture_m3 import (HardwareStop, Receiver, check_owners, commander, drain, inventory,
                        select_uart, sha, verify_manifest)
from m3_health_oracle import require
from m3_errno import verify_errno_map
from m3_network_oracle import checkpoint, records, validate_stage

SERIES_DEADLINE = float('inf')


class QualificationFailure(ValueError):
    pass


USB_SERIAL = '02290000325182c300000000000000000000000097969905'


def run_checked(command, destination):
    result = inventory(command, destination)
    destination.write_text(result.stdout + result.stderr)
    require(result.returncode == 0 and not result.stderr, 'Incomplete host inventory')
    return result.stdout


def _check_route(directory, name):
    interface = run_checked(['ifconfig', 'en5'], directory / (name + '-interface.txt'))
    route = run_checked(['route', '-n', 'get', '192.168.77.2'], directory / (name + '-route.txt'))
    require('ether 00:0a:cd:41:89:cc' in interface and
            re.search(r'inet 192\.168\.77\.1 netmask 0xffffff00\b', interface) and
            re.search(r'interface:\s+en5\b', route), 'Dedicated address/route/MAC mismatch')
    return 'status: active' in interface


def check_route(directory, name):
    try:
        return _check_route(directory, name)
    except HardwareStop:
        raise
    except Exception as error:
        raise HardwareStop('Isolated interface/route unverified; no target changes') from error


def check_link(directory, name):
    interface = run_checked(['ifconfig', 'en5'], directory / (name + '-interface.txt'))
    if 'ether 00:0a:cd:41:89:cc' not in interface:
        raise HardwareStop('Dedicated adapter identity changed')
    return 'status: active' in interface


def usb_association(tree):
    disks, uarts = [], []
    def walk(value, usb=False):
        if isinstance(value, dict):
            if 'USB Serial Number' in value:
                usb = (value.get('USB Serial Number') == USB_SERIAL and
                       value.get('idVendor') == 0x0d28 and value.get('idProduct') == 0x0204)
            if usb and value.get('BSD Name'): disks.append(value['BSD Name'])
            if usb and value.get('IOCalloutDevice'): uarts.append(value['IOCalloutDevice'])
            for child in value.values(): walk(child, usb)
        elif isinstance(value, list):
            for child in value: walk(child, usb)
    walk(tree)
    return disks, uarts


def _guard_check(session, directory, name, remaining=90):
    """Bind a current root-created guard record; fail before owner queries on mounted media."""
    require(session.get('clock') == time.CLOCK_NAME, 'Guard/runner clocks differ')
    require(session['serial'] == USB_SERIAL and session['disk'].startswith('/dev/disk'), 'Wrong mount guard identity')
    if 'boot_identity' in session:
        current_boot = run_checked(['/usr/sbin/sysctl', '-n', 'kern.boottime'],
                                   directory / (name + '-boot.txt'))
        require(current_boot == session['boot_identity'], 'Guard belongs to a different host boot')
    require(sha(session['executable']) == session['sha256'] and
            time.monotonic() + remaining < session['deadline'], 'Guard changed/expired or insufficient budget')
    status = run_checked(['ps', '-p', str(session['pid']), '-o', 'pid=,comm=,lstart='],
                         directory / (name + '-guard.txt'))
    require(status == session['process_identity'], 'Guard PID/start identity changed')
    log = Path(session['log']).read_text()
    require('DENIED exact_DAPLink_USB_serial=' + USB_SERIAL + ' disk=' +
            session['disk'].removeprefix('/dev/') in log and 'STOPPED' not in log,
            'Guard has no actual current-session veto evidence')
    query = inventory(['/usr/sbin/ioreg', '-p', 'IOService', '-l', '-w0', '-a'],
                      directory / (name + '-usb-timeout.json'))
    require(query.returncode == 0 and not query.stderr, 'USB ancestry inventory incomplete')
    disks, uarts = usb_association(plistlib.loads(query.stdout.encode()))
    (directory / (name + '-usb.json')).write_text(json.dumps(dict(disks=disks, uarts=uarts)))
    require(disks == [session['disk'].removeprefix('/dev/')] and
            uarts == ['/dev/cu.usbmodem11302'], 'Current disk/UART USB ancestry changed')
    mounts = run_checked(['mount'], directory / (name + '-mounts.txt'))
    require(not re.search(r'(?m)^' + re.escape(session['disk']) + r'(?:s[0-9]+)* on ', mounts), 'DAPLink virtual media mounted; stop before UART inventory')


def guard_check(session, directory, name, remaining=90):
    try:
        _guard_check(session, directory, name, remaining)
    except HardwareStop:
        raise
    except Exception as error:
        raise HardwareStop('Current mount protection unverified; no target changes') from error


def finish_receiver(receiver):
    try:
        receiver.finish()
    except Exception as error:
        raise HardwareStop('UART capture integrity uncertain; no target changes') from error


def wait_until(predicate, duration, receiver=None):
    deadline = min(time.monotonic() + duration, SERIES_DEADLINE)
    while time.monotonic() < deadline:
        if receiver and receiver.failure:
            raise HardwareStop('UART reader failed: ' + repr(receiver.failure))
        if predicate(): return
        time.sleep(.05)
    raise ValueError('Declared observation deadline expired')


def fresh_network_rows(rows, source, entry_profile='network'):
    banners = [i for i, (_, line) in enumerate(rows) if line.startswith('*** Booting Zephyr')]
    require(len(banners) <= 1, 'Duplicate boot during initial network checkpoint')
    if not banners:
        return []
    prefix = rows[:banners[0]]
    require(entry_profile in ('network', 'normal'), 'Unknown pre-reset image role')
    normal_patterns = (
        r'M3_WORK sequence=\d+ monotonic_ms=\d+ time=-?\d+ pressed=[01] device=-?\d+',
        r'M3_OBSERVER_ALIVE monotonic_ms=\d+ healthy=1 sequence=\d+ readiness=0',
        r'M3_STACK worker_status=0 worker_object=4160 worker_usable=4096 worker_unused=\d+ main_status=0 main_usable=2048 main_unused=\d+',
        r'Thread analyze:',
        r'(?:m3-critical|idle|main|ISR0)\s*: STACK: unused \d+ usage \d+ / \d+ \(\d+ %\); CPU: \d+ %',
        r': Total CPU cycles used: \d+',
    )
    require(all(any(re.fullmatch(pattern, line) for pattern in normal_patterns)
                if entry_profile == 'normal' else
                any(line.startswith(name + ' ') for name in
                    ('M3_NET_RESOURCE', 'M3_NET_STATE', 'M3_NET_STACK'))
                for _, line in prefix), 'Unexpected pre-reset activity')
    require(all(r['service_ready'] == 0 for r in records(prefix, 'M3_NET_STATE')),
            'Pre-reset service was active')
    fresh = rows[banners[0]:]
    boots = records(fresh, 'M3_NET_BOOT')
    require(len(boots) <= 1, 'Duplicate network image identity')
    if not boots:
        return []
    require(0 <= boots[0]['received'] - fresh[0][0] <= .5, 'Late fresh image identity')
    require(boots[0]['source'] == source and boots[0]['profile'] == 'network-probe',
            'Wrong fresh network image identity')
    return fresh


def initial_checkpoint_boundary(rows):
    ready = [r for r in records(rows, 'M3_NET_STATE') if r['monotonic_ms'] >= 1000 and
             r['link'] == r['address'] == 1 and r['service_ready'] == 0]
    require(ready, 'No fresh ready-link idle checkpoint boundary')
    return ready[0]['received'] + .25


def stop_peer(process):
    if process.poll() is not None:
        raise HardwareStop('Peer expired/exited before explicit stage teardown')
    process.send_signal(signal.SIGTERM)
    try:
        require(process.wait(timeout=3) == 0, 'Peer exit failed')
    except subprocess.TimeoutExpired as error:
        os.killpg(process.pid, signal.SIGKILL)
        try: process.wait(timeout=1)
        except subprocess.TimeoutExpired: pass
        raise HardwareStop('Peer failed bounded teardown; no target changes') from error


def release_peer(lease):
    # Detach before any fallible teardown: later cleanup paths cannot retry it.
    process, lease['process'] = lease.get('process'), None
    log, lease['log'] = lease.get('log'), None
    try:
        if process is not None: stop_peer(process)
    finally:
        if log is not None: log.close()


def image_commands(binary, programming):
    path = str(binary)
    require('"' not in path and '\n' not in path, 'Unsafe image command path')
    return (f'r\nh\nloadfile "{path}", 0x60000000\nverifybin "{path}", 0x60000000\n'
            if programming else '') + 'r\ng\nq\n'


def select_entry_image(choice, image, recovery, previous=None):
    require(choice in ('original-normal', 'existing-network', 'previous-network'), 'Unknown entry image')
    if choice == 'previous-network':
        require(previous is not None and previous['profile'] == 'network', 'Explicit previous network manifest required')
        return previous, True
    require(previous is None, 'Unexpected previous image manifest')
    return (image, False) if choice == 'existing-network' else (recovery, True)


def stage_modes(diagnostic):
    return ('echo',) if diagnostic else ('echo', 'silence', 'echo', 'close', 'echo', 'echo', 'echo')


def completed_series(diagnostic, completed):
    require(completed == len(stage_modes(diagnostic)), 'Incomplete selected series')
    return dict(status='DIAGNOSTIC_PASS' if diagnostic else 'PASS', stages=completed,
                qualification=not diagnostic,
                recovery='original standalone normal final' if diagnostic else
                'explicit restart for link restoration; original standalone normal final')


def phy_diagnostic_mode(config, diagnostic):
    enabled = 'CONFIG_CRUX_M3_PHY_DIAGNOSTIC=y' in Path(config).read_text().splitlines()
    require(diagnostic or not enabled, 'PHY diagnostic image cannot qualify seven stages')
    return enabled


def phy_diagnostic_result(rows):
    reads = records(rows, 'M3_NET_PHY_READ')
    done = records(rows, 'M3_NET_PHY_DONE')
    mac = records(rows, 'M3_NET_MAC')
    cached = records(rows, 'M3_NET_PHY_CACHED')
    require(len(done) == 1 and done[0]['reads'] == 8 and done[0]['status'] == 0
            and done[0]['plausible'] == 1, 'PHY diagnostic missing or uncertain')
    require(len(reads) == 8 and [r['index'] for r in reads] == list(range(8))
            and [r['reg'] for r in reads] == [0, 1, 1, 2, 3, 4, 5, 31]
            and all(r['result'] == 0 and 0 <= r['value'] <= 65535 for r in reads),
            'Incomplete or invalid PHY read sequence')
    require(reads[3]['value'] == 0x22 and reads[4]['value'] not in (0, 65535),
            'Implausible PHY identity; native MDIO return alone is insufficient')
    require(len(mac) == 2 and [r['phase'] for r in mac] == [1, 2]
            and len(cached) == 1 and cached[0]['result'] == 0,
            'Missing MAC or cached PHY observations')
    fields = ('eir', 'eimr', 'ecr', 'rcr', 'tcr', 'rdar', 'tdar', 'mscr', 'mmfr')
    require(all(all(key in r and isinstance(r[key], int) and 0 <= r[key] <= 0xffffffff
                        for key in fields) for r in mac)
            and all(key in cached[0] and cached[0][key] in (0, 1)
                    for key in ('link', 'admin', 'running', 'carrier'))
            and all(key in cached[0] and isinstance(cached[0][key], int)
                    for key in ('speed', 'oper')), 'Incomplete MAC or cached PHY fields')
    require(mac[0]['monotonic_ms'] <= mac[1]['monotonic_ms'] <= cached[0]['monotonic_ms']
            <= reads[0]['monotonic_ms'] and
            all(a['monotonic_ms'] <= b['monotonic_ms'] for a, b in zip(reads, reads[1:]))
            and reads[-1]['monotonic_ms'] <= done[0]['monotonic_ms'],
            'Reordered PHY diagnostic observations')
    return dict(status='OBSERVED_PLAUSIBLE_NOT_TRANSACTION_PROOF', reads=8)


def main():
    global SERIES_DEADLINE
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--manifest', type=Path, required=True)
    parser.add_argument('--errno-map', type=Path, required=True)
    parser.add_argument('--entry-image', choices=('original-normal', 'existing-network', 'previous-network'), default='original-normal')
    parser.add_argument('--entry-manifest', type=Path)
    parser.add_argument('--recovery-manifest', type=Path, required=True)
    parser.add_argument('--guard-session', type=Path, required=True)
    parser.add_argument('--evidence', type=Path, required=True)
    parser.add_argument('--uart', default='/dev/cu.usbmodem11302')
    parser.add_argument('--diagnostic-echo-only', action='store_true',
                        help='One echo round and normal restoration; never a qualification PASS')
    args = parser.parse_args()
    image, recovery = verify_manifest(args.manifest), verify_manifest(args.recovery_manifest)
    require(image['profile'] == 'network' and recovery['profile'] == 'normal', 'Wrong image roles')
    previous = verify_manifest(args.entry_manifest) if args.entry_manifest else None
    existing, programming = select_entry_image(args.entry_image, image, recovery, previous)
    require(sha(recovery['binary']) == '238b352fd66bbfea984c4210510dd398cfe808fcb93c5f11a142fa4ba5bd24c0', 'Original recovery identity changed')
    from m3_image import check_record_output
    check_record_output(image['config'])
    phy_diagnostic = phy_diagnostic_mode(image['config'], args.diagnostic_echo_only)
    errno_abi = verify_errno_map(args.errno_map, image)
    repo = Path(__file__).resolve().parents[2]
    require(not subprocess.check_output(['git', 'status', '--porcelain'], cwd=repo, text=True).strip(),
            'Physical runner source must be committed and clean')
    args.evidence.mkdir(parents=True, exist_ok=False)
    directory = args.evidence
    session = json.loads(args.guard_session.read_text())
    (directory / 'inputs.json').write_text(json.dumps(dict(image=image, recovery=recovery, entry_manifest=existing,
        guard_session=session, clock=time.identity(), errno_abi=errno_abi, errno_map_sha256=sha(args.errno_map), entry_image=args.entry_image, runner_commit=subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=repo, text=True).strip(),
        diagnostic_echo_only=args.diagnostic_echo_only,
        scripts={p.name: sha(p) for p in Path(__file__).parent.glob('*.py')}), indent=2))
    end = time.monotonic() + 1800
    SERIES_DEADLINE = end
    guard_check(session, directory, 'entry', 1850)
    service = run_checked(['networksetup', '-getinfo', 'AX88179'], directory / 'entry-service.txt')
    require('IP address: 192.168.77.1' in service and 'Subnet mask: 255.255.255.0' in service and
            'Router: (null)' in service and 'Ethernet Address: 00:0a:cd:41:89:cc' in service,
            'Dedicated adapter service must have reviewed static address and no gateway')
    check_link(directory, 'entry')
    import serial
    import serial.tools.list_ports
    with (directory.parent / 'evkb-602003106.lock').open('a') as lock:
        fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
        check_owners(args.uart, directory / 'owners.txt')
        ports = select_uart([vars(p) for p in serial.tools.list_ports.comports()], args.uart, USB_SERIAL)
        (directory / 'ports.json').write_text(json.dumps(ports, default=str, indent=2))
        with serial.Serial(args.uart, 115200, timeout=.02, exclusive=True, write_timeout=1) as port:
            fcntl.ioctl(port.fileno(), termios.TIOCEXCL)
            lease = dict(process=None, log=None)
            def target(commands, label):
                if time.monotonic() + 120 >= end:
                    raise HardwareStop('Insufficient whole-series inventory/probe/cleanup budget')
                guard_check(session, directory, label)
                require(time.monotonic() + 45 < end, 'Insufficient whole-series probe/cleanup budget')
                try:
                    text = commander('602003106', commands, directory / (label + '.log'))
                    require('CPUID register: 0x411FC271' in text and 'DPIDR: 0x0BD11477' in text and
                            re.search(r'S/N:\s*602003106\b', text) and 'Cortex-M7' in text,
                            'Target/probe identity mismatch')
                    voltage = re.search(r'VTref\s*=\s*([\d.]+)\s*V', text)
                    require(voltage and 3 <= float(voltage[1]) <= 3.6, 'Target voltage mismatch')
                    return text
                except HardwareStop:
                    raise
                except Exception as error:
                    raise HardwareStop('Probe identity/evidence unverified; stop target changes') from error
            def boot(manifest, name, programming, entry_profile='network'):
                path = Path(manifest['binary'])
                require('"' not in str(path) and '\n' not in str(path), 'Unsafe image path')
                capture = directory / name
                capture.mkdir()
                drain(port, capture)
                receiver = Receiver(port, capture)
                receiver.begin()
                try:
                    commands = image_commands(path, programming)
                    text = target(commands, name)
                    if programming and 'Verify successful' not in text:
                        raise HardwareStop('Complete image verification absent')
                    source = manifest['source']['git_commit'][:12]
                    if manifest['profile'] == 'network':
                        wait_until(lambda: f'M3_NET_BOOT source={source} profile=network-probe' in receiver.text(), 10, receiver)
                        wait_until(lambda: any(r['monotonic_ms'] >= 1000 and
                            r['link'] == r['address'] == 1 and r['service_ready'] == 0
                            for r in records(fresh_network_rows(receiver.rows, source, entry_profile), 'M3_NET_STATE')), 15, receiver)
                        checkpoint_start = initial_checkpoint_boundary(fresh_network_rows(receiver.rows, source, entry_profile))
                        wait_until(lambda: time.monotonic() >= checkpoint_start + 4.5, 5, receiver)
                        require(receiver.text().count('*** Booting Zephyr') == 1 and
                                'M3_NET_INIT result=0 ' in '\n'.join(s for _, s in fresh_network_rows(receiver.rows, source, entry_profile)) and
                                'M3_NET_BASELINE contexts=0' in '\n'.join(s for _, s in fresh_network_rows(receiver.rows, source, entry_profile)), 'Wrong/failed fresh network boot')
                        require(not records(fresh_network_rows(receiver.rows, source, entry_profile), 'M3_NET_EXCHANGE'), 'Automatic application traffic at boot')
                    else:
                        from capture_m3 import normal_result
                        wait_until(lambda: len(records(receiver.rows, 'M3_OBSERVER_ALIVE')) >= 5, 15, receiver)
                finally:
                    finish_receiver(receiver)
                    (capture / 'lines.json').write_text(json.dumps(receiver.rows, indent=2))
                if manifest['profile'] == 'normal':
                    banners = [i for i, (_, s) in enumerate(receiver.rows) if s.startswith('*** Booting Zephyr')]
                    require(len(banners) == 1, 'Missing/duplicate recovery boot')
                    result = normal_result(receiver.rows[banners[0]:], source)
                else:
                    fresh = fresh_network_rows(receiver.rows, source, entry_profile)
                    require(fresh, 'Fresh network boot absent')
                    checkpoint(fresh, checkpoint_start, 0)
                    result = dict(status='PASS', profile='network', no_automatic_traffic=True)
                (capture / 'result.json').write_text(json.dumps(result, indent=2))
            def acknowledge(label):
                path = directory / (label + '.json')
                (directory / 'operator-request.json').write_text(json.dumps(dict(action=label, deadline=time.monotonic()+90)))
                print('OPERATOR_ACTION ' + label, flush=True)
                wait_until(path.exists, min(90, max(0, end-time.monotonic())))
                ack = json.loads(path.read_text())
                require(ack.get('action') == label and ack.get('confirmed') is True, 'Cable action not confirmed')
                return time.monotonic()
            target('q\n', 'entry-identity-only')
            known = target(f'verifybin "{existing["binary"]}", 0x60000000\nIsHalted\nq\n', 'entry-known-image')
            if 'Verify successful' not in known:
                raise HardwareStop('Current firmware differs from explicitly selected known image; stop before changes')
            boot(image, 'program-network' if programming else 'resume-network-reset', programming,
                 existing['profile'])
            try:
                completed = 0
                for stage, mode in enumerate(stage_modes(args.diagnostic_echo_only), 1):
                    guard_check(session, directory, f'stage-{stage}')
                    require(time.monotonic() + (650 if stage == 6 else 400 if stage == 7 else 220) < end,
                            'Insufficient series budget including operator/recovery bounds')
                    case = directory / f'stage-{stage}'
                    case.mkdir()
                    if stage == 7:
                        acknowledge('reconnect-evkb-rj45')
                        wait_until(lambda: check_link(case, 'reconnect'), 15)
                        time.sleep(1)
                        require(check_route(case, 'restored-route'), 'Restored link not active')
                        boot(image, 'explicit-link-recovery-restart', False)
                    else:
                        check_route(case, 'entry')
                    peer_output = case / 'peer.jsonl'
                    lease['log'] = (case / 'peer-process.log').open('wb')
                    lease['process'] = subprocess.Popen([sys.executable, str(Path(__file__).with_name('m3_network_peer.py')),
                        '--mode', mode, '--duration', '300' if stage == 6 else '180', '--output', str(peer_output)],
                        stdout=lease['log'], stderr=subprocess.STDOUT, start_new_session=True)
                    try:
                        wait_until(lambda: peer_output.exists() and 'peer_ready' in peer_output.read_text(), 3)
                    except Exception as error:
                        raise HardwareStop('Peer readiness unverified') from error
                    receiver = Receiver(port, case)
                    receiver.begin()
                    try:
                        if stage == 6:
                            confirmed = acknowledge('unplug-evkb-rj45')
                            wait_until(lambda: not check_link(case, 'link-down') and any(
                                r['received'] >= confirmed and r['link'] == r['service_ready'] == 0
                                for r in records(receiver.rows, 'M3_NET_STATE')), 15, receiver)
                        start = len(receiver.rows)
                        written = time.monotonic()
                        if time.monotonic() + 160 >= end:
                            raise HardwareStop('Insufficient remaining round budget')
                        try:
                            require(port.write(b'm3net\r') == 6, 'Incomplete UART command')
                        except Exception as error:
                            raise HardwareStop('UART command transport unverified') from error
                        (case / 'transmit.json').write_text(json.dumps(dict(command='m3net', monotonic=written)))
                        wait_until(lambda: bool(records(receiver.rows[start:], 'M3_NET_ROUND')), 155, receiver)
                        summary = records(receiver.rows[start:], 'M3_NET_ROUND')[0]
                        if phy_diagnostic:
                            try:
                                wait_until(lambda: bool(records(receiver.rows[start:], 'M3_NET_PHY_DONE')),
                                           5, receiver)
                                observed = phy_diagnostic_result(receiver.rows[start:])
                                (case / 'phy-diagnostic.json').write_text(json.dumps(observed, indent=2))
                            except Exception as error:
                                raise HardwareStop('PHY observation incomplete or uncertain; no target changes') from error
                        wait_until(lambda: time.monotonic() >= summary['received'] + 4.5, 5, receiver)
                    finally:
                        finish_receiver(receiver)
                        (case / 'lines.json').write_text(json.dumps(receiver.rows, indent=2))
                        release_peer(lease)
                    frames = [json.loads(s) for s in peer_output.read_text().splitlines()]
                    acknowledgements = [i for i, (_, line) in enumerate(receiver.rows[start:], start)
                                        if 'M3_NET_REQUESTED' in line]
                    require(len(acknowledgements) == 1, 'Missing/duplicate explicit round acknowledgement')
                    qualified_rows = receiver.rows[acknowledgements[0]:]
                    try:
                        result = validate_stage(qualified_rows, frames, 1 if stage == 7 else stage, mode, stage == 6, errno_abi=errno_abi)
                    except ValueError as error:
                        raise QualificationFailure(str(error)) from error
                    (case / 'result.json').write_text(json.dumps(result, indent=2))
                    completed += 1
                    print('STAGE_PASS ' + str(stage), flush=True)
            except Exception as error:
                (directory / 'qualification-failure.txt').write_text(traceback.format_exc())
                release_peer(lease)
                if isinstance(error, QualificationFailure):
                    boot(recovery, 'failed-series-normal-restoration', True)
                    raise
                if isinstance(error, HardwareStop): raise
                raise HardwareStop('Unverified host/peer/transport state; no automatic restoration') from error
            else:
                boot(recovery, 'final-normal-restoration', True)
                (directory / 'result.json').write_text(json.dumps(
                    completed_series(args.diagnostic_echo_only, completed), indent=2))
            finally:
                release_peer(lease)



if __name__ == '__main__': main()
