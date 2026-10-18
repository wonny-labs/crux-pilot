"""Explicit bounded M3 watchdog capture; requires current authority and physical-plan PASS."""
import argparse
import fcntl
import hashlib
import json
from pathlib import Path
from datetime import datetime, timezone
import platform
import os
import signal
import tempfile
import re
import subprocess
import sys
import traceback
import termios
import threading
import m3_clock as time

from m3_health_oracle import require, validate, validate_stacks
from m3_image import check_image


class HardwareStop(RuntimeError):
    pass


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def verify_jlink_tools(manifest=None):
    manifest = Path(manifest) if manifest else Path(__file__).with_name('jlink-tools.json')
    records = json.loads(manifest.read_text())
    require(set(records) == {'executable', 'dll'}, 'Incomplete J-Link identity manifest')
    for record in records.values():
        path = Path(record['path'])
        require(path.is_absolute() and path.resolve() == path and
                sha(path) == record['sha256'], 'J-Link tool identity changed; no probe access')
    alias = Path(records['dll']['alias'])
    require(alias.is_absolute() and alias.resolve() == Path(records['dll']['path']),
            'J-Link DLL alias changed; no probe access')
    return records['executable']['path']


def _commander(probe, commands, output):
    executable = verify_jlink_tools()
    require(probe == '602003106', 'Unexpected probe selection')
    script = output.with_suffix('.jlink')
    script.write_text('exec DisableAutoUpdateFW\nusb ' + probe + '\nconnect\n' + commands)
    environment = {key: value for key, value in os.environ.items() if not key.startswith('DYLD_')}
    with output.open('wb') as log:
        command = [executable, '-Device', 'MIMXRT1062xxx6A', '-If', 'SWD', '-Speed', '400',
                   '-NoGui', '1', '-ExitOnError', '1', '-AutoConnect', '0', '-CommandFile', str(script)]
        process = subprocess.Popen(command, stdout=log, stderr=subprocess.STDOUT,
                                   start_new_session=True, env=environment)
        try:
            code = process.wait(timeout=30)
        except subprocess.TimeoutExpired as error:
            cleanup = []
            try:
                os.killpg(process.pid, signal.SIGKILL)
            except OSError as failure:
                cleanup.append(repr(failure))
            try:
                process.wait(timeout=1)
            except subprocess.TimeoutExpired:
                cleanup.append('created process not reaped; no retry')
            output.with_suffix('.timeout.json').write_text(json.dumps(dict(
                status='PROBE_STATE_UNCERTAIN', pid=process.pid, pgid=process.pid,
                cleanup=cleanup), indent=2))
            raise HardwareStop('Probe timeout; state uncertain; no further hardware changes') from error
        require(code == 0, 'Probe exited unsuccessfully; state uncertain')
    text = output.read_text(errors='replace')
    if re.search(r'Updating firmware:|Replacing firmware:|Waiting for new firmware', text):
        raise HardwareStop('Unexpected probe firmware maintenance; stop all target operations')
    diagnostics = '\n'.join(line for line in text.splitlines()
                            if line.strip() != 'J-Link Commander will now exit on Error')
    if re.search(r'(?i)\b(error|failed|failure|could not|cannot|warning|unable|unknown command|timed out)\b', diagnostics):
        raise HardwareStop('Probe diagnostic; stop hardware changes')
    return text


def commander(probe, commands, output):
    try:
        return _commander(probe, commands, output)
    except HardwareStop:
        raise
    except Exception as error:
        raise HardwareStop('Probe command or its evidence failed; stop hardware changes') from error


class Receiver:
    def __init__(self, port, directory):
        self.port = port
        self.directory = directory
        self.rows = []
        self.failure = None
        self.stop = threading.Event()
        self.ready = threading.Event()
        self.pending_empty = True
        self.last_receive = 0
        self.thread = threading.Thread(target=self.read, daemon=True)

    def read(self):
        pending = b''
        previous = time.monotonic()
        try:
            with (self.directory / 'uart-rx.bin').open('wb') as raw, \
                 (self.directory / 'receive.jsonl').open('w') as events, \
                 (self.directory / 'poll.jsonl').open('w') as polls:
                (self.directory / 'clock.json').write_text(json.dumps(time.identity(), indent=2))
                self.ready.set()
                while not self.stop.is_set():
                    chunk = self.port.read(4096)
                    now = time.monotonic()
                    polls.write(json.dumps({'monotonic': now, 'gap': now - previous}) + '\n')
                    require(0 <= now - previous <= .25, 'Serial reader discontinuity')
                    previous = now
                    if not chunk:
                        continue
                    self.last_receive = now
                    raw.write(chunk)
                    raw.flush()
                    events.write(json.dumps({'monotonic': now, 'hex': chunk.hex()}) + '\n')
                    events.flush()
                    pending += chunk
                    while b'\n' in pending:
                        line, pending = pending.split(b'\n', 1)
                        # Preserve original bytes above; normalise CR and documented shell ANSI only.
                        line = re.sub(rb'\x1b\[[0-9;]*[A-Za-z]', b'', line).strip()
                        if line:
                            self.rows.append((now, line.decode('ascii', errors='strict')))
                    self.pending_empty = not pending.strip()
                require(not pending.strip(), 'Incomplete final serial line')
        except BaseException as error:
            self.failure = error

    def begin(self):
        self.thread.start()
        require(self.ready.wait(1), 'Serial reader did not initialise')

    def finish(self):
        deadline = time.monotonic() + .25
        while time.monotonic() < deadline and (not self.pending_empty or time.monotonic() - self.last_receive < .025):
            time.sleep(.005)
        self.stop.set()
        self.thread.join(1)
        require(not self.thread.is_alive(), 'Serial reader did not stop')
        if self.failure:
            raise self.failure

    def text(self):
        return '\n'.join(line for _, line in self.rows)


def drain(port, directory):
    started = quiet = time.monotonic()
    with (directory / 'pre-reset-uart.bin').open('wb') as raw:
        while time.monotonic() - started < 3:
            chunk = port.read(4096)
            if chunk:
                raw.write(chunk)
                quiet = time.monotonic()
            if time.monotonic() - quiet >= .05:
                return
    raise ValueError('Unable to drain before fresh reset')


def normal_result(rows, source):
    text = '\n'.join(s for _, s in rows)
    require(rows and rows[0][1].startswith('*** Booting Zephyr'), 'Stale normal capture')
    require(text.count('*** Booting Zephyr') == 1, 'Normal reset loop')
    require(text.count(f'M3_BOOT source={source} profile=independent-health commands=0 fault_default=off') == 1,
            'Wrong normal image')
    require(not re.search(r'M3_WDT_|M3_FAULT_|FAILED|FATAL ERROR', text), 'Unsafe normal image')
    observers = [re.fullmatch(r'M3_OBSERVER_ALIVE monotonic_ms=(\d+) healthy=1 sequence=(\d+) readiness=0', s)
                 for _, s in rows if s.startswith('M3_OBSERVER_ALIVE ')]
    healthy = [m for m in observers if m]
    require(len(healthy) >= 4 and int(healthy[-1][1]) - int(healthy[0][1]) >= 3000,
            'Normal healthy progress absent')
    require(int(healthy[-1][2]) > int(healthy[0][2]), 'Normal work not advancing')
    first_healthy = next(i for i, (_, s) in enumerate(rows) if s == healthy[0][0])
    require(all(re.fullmatch(r'M3_OBSERVER_ALIVE monotonic_ms=\d+ healthy=1 sequence=\d+ readiness=0', s)
                for _, s in rows[first_healthy:] if s.startswith('M3_OBSERVER_ALIVE ')),
            'Non-continuous normal recovery')
    reset = [s for _, s in rows if s.startswith('M3_RESET ')]
    require(len(reset) == 1, 'Missing/duplicate normal reset cause')
    cause = re.fullmatch(r'M3_RESET support_status=0 supported=0x([0-9a-f]+) cause_status=0 cause=0x[0-9a-f]+ clear_status=0 remaining_status=0 remaining=0x0', reset[0])
    require(cause and int(cause[1], 16) & 0x10, 'Reset reporting/clearing unavailable before watchdog use')
    work = []
    for _, line in rows:
        if line.startswith('M3_WORK '):
            match = re.fullmatch(r'M3_WORK sequence=(\d+) monotonic_ms=(\d+) time=(-?\d+) pressed=([01]) device=0', line)
            require(match and int(match[3]) in (0, -4), 'Required normal work failed')
            work.append((int(match[1]), int(match[2])))
    require(len(work) >= 4 and work[-1][1] - work[0][1] >= 3000 and work[-1][0] > work[0][0],
            'Required normal work did not progress')
    stacks = validate_stacks(rows)
    return {'worker_min_unused': min(s[0] for s in stacks), 'main_min_unused': min(s[1] for s in stacks), 'status': 'PASS', 'source': source, 'commands': 0, 'fault_default': 'off'}


def _capture(port, probe, directory, source, watchdog):
    directory.mkdir()
    drain(port, directory)
    receiver = Receiver(port, directory)
    receiver.begin()
    started = time.monotonic()
    transmissions = []
    result = None
    try:
        # Capture stays live during the established reset/start command.
        commander(probe, 'r\ng\nq\n', directory / 'reset.log')
        boot = arm = post_boot = None
        sent_arm = sent_stall = False
        while time.monotonic() - started <= (30 if watchdog else 8):
            require(receiver.failure is None, f'Serial reader failed: {receiver.failure}')
            all_rows = list(receiver.rows)
            banners = [i for i, (_, s) in enumerate(all_rows) if s.startswith('*** Booting Zephyr')]
            rows = all_rows[banners[0]:] if banners else []
            prefix = all_rows[:banners[0]] if banners else all_rows
            require(not any(re.search(r'M3_BOOT|M3_WDT_|M3_FAULT_', s) for _, s in prefix),
                    'Unexplained boot/armed output before fresh reset banner')
            boots = [(t, s) for t, s in rows if s.startswith('M3_BOOT ')]
            if boots and boot is None:
                boot = boots[0][0]
            if not watchdog:
                if boot is not None and time.monotonic() - boot >= 5:
                    result = normal_result(rows, source)
                    break
            else:
                text = receiver.text()
                require(not re.search(r'M3_\w*(?:FAILED|REJECTED)|FATAL ERROR', text), 'Runtime failure')
                if boot is not None and not sent_arm and time.monotonic() - boot >= 2.5:
                    transmissions.append({'monotonic': time.monotonic(), 'text': 'm3 arm'})
                    port.write(b'm3 arm\r')
                    port.flush()
                    sent_arm = True
                arms = [t for t, s in rows if s.startswith('M3_WDT_ARMED ')]
                if arms and arm is None:
                    arm = arms[0]
                if arm is not None and not sent_stall and time.monotonic() - arm >= 12.5:
                    transmissions.append({'monotonic': time.monotonic(), 'text': 'm3 stall'})
                    port.write(b'm3 stall\r')
                    port.flush()
                    sent_stall = True
                require(len(boots) <= 2, 'Unexpected repeated boot')
                if len(boots) == 2:
                    require(sent_stall, 'Unintended watchdog reset')
                    post_boot = boots[1][0]
                if post_boot is not None and time.monotonic() - post_boot >= 4.3:
                    result = validate(rows, source)
                    break
            time.sleep(.02)
        require(result is not None, 'Capture liveness bound exceeded')
        result['preserved_pre_boot_rows'] = len(prefix)
    finally:
        primary = sys.exc_info()[1]
        try:
            try:
                try:
                    receiver.finish()
                except Exception as finish_error:
                    (directory / 'reader-stop-error.txt').write_text(repr(finish_error) + '\n')
                    if primary is None:
                        raise
            finally:
                (directory / 'transmit.json').write_text(json.dumps(transmissions, indent=2) + '\n')
                (directory / 'lines.json').write_text(json.dumps(receiver.rows, indent=2) + '\n')
                (directory / 'uart.txt').write_text(receiver.text() + '\n')
        except Exception as cleanup_error:
            if primary is None:
                raise
            primary.evidence_failure = repr(cleanup_error)
    banners = [i for i, (_, s) in enumerate(receiver.rows) if s.startswith('*** Booting Zephyr')]
    require(banners, 'No fresh native boot')
    final_rows = list(receiver.rows[banners[0]:])
    result = validate(final_rows, source) if watchdog else normal_result(final_rows, source)
    result['preserved_pre_boot_rows'] = banners[0]
    (directory / 'result.json').write_text(json.dumps(result, indent=2) + '\n')
    return result


def capture(port, probe, directory, source, watchdog):
    try:
        return _capture(port, probe, directory, source, watchdog)
    except Exception as error:
        try:
            if directory.is_dir():
                (directory / 'failure.txt').write_text(traceback.format_exc())
                (directory / 'result.json').write_text(json.dumps(
                    {'status': 'FAIL', 'error_type': type(error).__name__, 'error': str(error)}, indent=2) + '\n')
        except Exception as write_error:
            error.evidence_failure = repr(write_error)
        raise


def verify_manifest(path):
    manifest = json.loads(path.read_text())
    require(manifest['target'] == 'mimxrt1060_evk@B//qspi', 'Wrong target')
    require(manifest['bin_address'] == 0x60000000 and
            (manifest.get('elf_sections_match_bin') is True or manifest.get('elf_segments_match_bin') is True), 'Wrong image bounds')
    if manifest.get('elf_sections_match_bin'):
        require(manifest.get('gap_fill') == 255, 'Unexpected pinned image gap fill')
    require(not manifest['source']['dirty'] and manifest['source']['recoverable_from_git'], 'Unfrozen source')
    for record in manifest['artifacts']:
        require(sha(record['path']) == record['sha256'], 'Changed image/configuration')
    require(manifest['profile'] in ('probe', 'normal', 'network', 'integrated-probe', 'integrated-normal'), 'Wrong image profile')
    required = [Path(manifest[name]).resolve() for name in ('binary', 'elf', 'config')]
    recorded = [Path(record['path']).resolve() for record in manifest['artifacts']]
    require(len(recorded) == len(set(recorded)) and all(p in recorded for p in required),
            'Firmware/ELF/config not uniquely bound to hashed records')
    check_image(manifest['binary'], manifest['elf'], manifest['config'], manifest['profile'])
    binary = Path(manifest['binary'])
    require(0 < binary.stat().st_size < 8 * 1024 * 1024, 'Wrong binary length')
    require('"' not in str(binary) and '\n' not in str(binary), 'Unsafe command path')
    return manifest


def run_series(program, observe):
    """Single series, no retry; programming/probe errors prohibit subsequent changes."""
    program('normal', 'program-normal-preflight')
    observe('normal-preflight', False)
    program('probe', 'program-probe')
    recover = True
    try:
        for number in range(1, 4):
            observe(f'trial-{number:02d}', True)
    except (HardwareStop, subprocess.SubprocessError):
        recover = False
        raise
    finally:
        if recover:
            program('normal', 'program-final-normal')
            observe('final-normal', False)


def inventory(command, output):
    """Fail closed without waiting indefinitely for an uninterruptible child."""
    with tempfile.TemporaryFile() as stdout, tempfile.TemporaryFile() as stderr:
        process = subprocess.Popen(command, stdout=stdout, stderr=stderr, start_new_session=True)
        try:
            code = process.wait(timeout=10)
        except subprocess.TimeoutExpired as error:
            cleanup = []
            try:
                os.killpg(process.pid, signal.SIGKILL)
            except OSError as failure:
                cleanup.append(repr(failure))
            try:
                process.wait(timeout=1)
            except subprocess.TimeoutExpired:
                cleanup.append('created process group not reaped within grace; no retry')
            stdout.seek(0)
            stderr.seek(0)
            output.write_text(json.dumps(dict(command=command, pid=process.pid, pgid=process.pid,
                                             status='OWNERSHIP_UNVERIFIED', cleanup=cleanup,
                                             stdout=stdout.read().decode(errors='replace'),
                                             stderr=stderr.read().decode(errors='replace')), indent=2))
            raise HardwareStop('Ownership inventory timed out; stop before target access') from error
        stdout.seek(0)
        stderr.seek(0)
        return subprocess.CompletedProcess(command, code, stdout.read().decode(errors='strict'),
                                           stderr.read().decode(errors='strict'))


def check_owners(uart, output):
    paths = [uart]
    if uart.startswith('/dev/cu.'):
        paths.append(uart.replace('/dev/cu.', '/dev/tty.', 1))
    owners = inventory(['lsof', *paths], output)
    output.write_text(owners.stdout + owners.stderr)
    require(owners.returncode == 1 and not owners.stdout and not owners.stderr,
            'UART already owned or ownership inventory uncertain')
    result = inventory(['ps', '-axo', 'pid=,comm=,args='], output.with_suffix('.probe-status.json'))
    require(result.returncode == 0 and not result.stderr and result.stdout.strip() and
            all(re.match(r'^\s*\d+\s+\S+', line) for line in result.stdout.splitlines()),
            'Probe ownership inventory uncertain')
    require(not re.search(r'(?im)(?:/|\s)(?:JLink\S*|openocd|pyocd(?:\.\S+)?)(?:\s|$)', result.stdout),
            'Probe already owned')


def select_uart(ports, uart, serial_number):
    """Bind the explicitly selected path to one verified USB serial identity."""
    require(bool(serial_number), 'Expected UART USB serial identity is required')
    matches = [p for p in ports if p.get('serial_number') == serial_number]
    require(len(matches) == 1 and matches[0].get('device') == uart and
            matches[0].get('vid') == 0x0d28 and matches[0].get('pid') == 0x0204,
            'UART mapping absent, ambiguous or wrong USB identity')
    return matches


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--probe-manifest', type=Path, required=True)
    parser.add_argument('--normal-manifest', type=Path, required=True)
    parser.add_argument('--recovery-bin', type=Path, required=True)
    parser.add_argument('--evidence', type=Path, required=True)
    parser.add_argument('--program', action='store_true', required=True)
    parser.add_argument('--uart', default='/dev/cu.usbmodem11102')
    parser.add_argument('--uart-serial', required=True)
    args = parser.parse_args()
    require(platform.system() == 'Darwin', 'Expected documented Mac hardware owner')
    verify_jlink_tools()
    require(sha(args.recovery_bin) == '4be80bc7cfcfc116bebba08d3d9c56b347cc6452548da299f7bf09a446ed55d2',
            'Known-good M2 recovery image identity changed')
    probe_id, uart = '602003106', args.uart
    probe_image = verify_manifest(args.probe_manifest)
    normal_image = verify_manifest(args.normal_manifest)
    require(probe_image['profile'] == 'probe' and normal_image['profile'] == 'normal', 'Wrong image roles')
    require(probe_image['source'] == normal_image['source'], 'Probe/normal source mismatch')
    source = probe_image['source']['git_commit'][:12]
    repo = Path(__file__).resolve().parents[2]
    require(not subprocess.check_output(['git', 'status', '--porcelain'], cwd=repo, text=True).strip(), 'Runner source not committed and clean')
    runner_commit = subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=repo, text=True).strip()
    args.evidence.mkdir(parents=True, exist_ok=False)
    records = {name: sha(Path(__file__).with_name(name)) for name in
               (Path(__file__).name, 'm3_health_oracle.py', 'm3_image.py', 'm3_clock.py', 'jlink-tools.json')}
    metadata = dict(executed_at_utc=datetime.now(timezone.utc).isoformat(), runner_commit=runner_commit,
                    runner_sha256=records, probe_manifest_sha256=sha(args.probe_manifest),
                    normal_manifest_sha256=sha(args.normal_manifest), recovery_bin_sha256=sha(args.recovery_bin),
                    probe=probe_id, uart=uart, uart_serial=args.uart_serial,
                    baud=115200, swd_khz=400, network='outside standalone health scope; no action',
                    scope='independent health probe; full M3 remains Pending')
    (args.evidence / 'inputs.json').write_text(json.dumps(metadata, indent=2) + '\n')
    lock_path = args.evidence.parent / 'evkb-602003106.lock'
    with lock_path.open('a') as lock:
        fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
        check_owners(uart, args.evidence / 'uart-owners.txt')
        import serial
        import serial.tools.list_ports
        ports = select_uart([vars(p) for p in serial.tools.list_ports.comports()],
                            uart, args.uart_serial)
        (args.evidence / 'serial-inventory.json').write_text(json.dumps(ports, default=str, indent=2) + '\n')
        with serial.Serial(uart, 115200, timeout=.02, exclusive=True) as port:
            fcntl.ioctl(port.fileno(), termios.TIOCEXCL)
            preflight = commander(probe_id, 'connect\nq\n', args.evidence / 'probe-preflight.log')
            volts = re.search(r'VTref\s*=\s*([\d.]+)\s*V', preflight)
            require(volts and 3 <= float(volts[1]) <= 3.6 and 'Cortex-M7' in preflight and
                    re.search(r'S/N:\s*602003106\b', preflight) and
                    'CPUID register: 0x411FC271' in preflight and 'DPIDR: 0x0BD11477' in preflight,
                    'Unexpected identity or voltage')
            state = {'status': 'PREFLIGHT_PASS', 'network': 'held; untouched', 'last_verified_image': None}

            def record_state():
                (args.evidence / 'state.json').write_text(json.dumps(state, indent=2) + '\n')

            record_state()

            def program(role, name):
                image = verify_manifest(args.probe_manifest if role == 'probe' else args.normal_manifest)
                binary = image['binary']
                state.update(status='PROGRAMMING', operation=name, target_state='unverified until readback')
                record_state()
                log = commander(probe_id, f'r\nh\nloadfile "{binary}", 0x60000000\nverifybin "{binary}", 0x60000000\nr\ng\nq\n',
                                args.evidence / f'{name}.log')
                if 'Verify successful' not in log:
                    raise HardwareStop('Full binary verification absent')
                state.update(status='PROGRAMMED_VERIFIED', last_verified_image=role, binary_sha256=sha(binary),
                             target_state='identified image started; observation pending')
                record_state()

            def observe(name, fault):
                state.update(status='CAPTURING', operation=name)
                record_state()
                result = capture(port, probe_id, args.evidence / name, source, fault)
                state.update(status='OBSERVATION_PASS', last_observation=name, result=result)
                if name == 'final-normal':
                    state['target_state'] = 'identified normal image running; fault commands absent'
                record_state()

            try:
                run_series(program, observe)
            except Exception as error:
                state.update(status='FAIL', error_type=type(error).__name__, error=str(error))
                try:
                    record_state()
                except Exception as write_error:
                    error.evidence_failure = repr(write_error)
                raise
            state['status'] = 'COMPONENT_PASS'
            record_state()
    print('Independent watchdog trials complete; M3 full acceptance remains Pending.')


if __name__ == '__main__':
    main()
