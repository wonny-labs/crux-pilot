"""One bounded, manually stimulated SW8 qualification on an identified normal image."""
import argparse
import fcntl
import json
from pathlib import Path
import platform
import re
import subprocess
import sys
import termios
import time
import traceback

from capture_m3 import (Receiver, check_owners, commander, drain, normal_result,
                        require, select_uart, sha, verify_jlink_tools, verify_manifest)


def fresh_rows(rows):
    banners = [i for i, (_, line) in enumerate(rows) if line.startswith('*** Booting Zephyr')]
    require(len(banners) == 1, 'Missing or repeated fresh boot')
    prefix = rows[:banners[0]]
    require(not any(re.search(r'M3_BOOT|M3_WDT_|M3_FAULT_', line) for _, line in prefix),
            'Unexplained unsafe pre-boot output')
    return rows[banners[0]:]


def released_baseline(rows, source, ready):
    baseline = [(received, line) for received, line in rows if received < ready]
    normal_result(baseline, source)
    source_index = next(i for i, (_, line) in enumerate(baseline) if line.startswith('M3_BOOT '))
    require(0 <= baseline[source_index][0] - baseline[0][0] <= .5 and
            all(i > source_index for i, (_, line) in enumerate(baseline) if line.startswith('M3_WORK ')),
            'Mismatched fresh source boot')
    work = [(t, line) for t, line in baseline if line.startswith('M3_WORK ')]
    require(len(work) >= 4 and all('pressed=0 device=0' in line for _, line in work),
            'Button must be released before GPIO cue')


def validate_gpio(rows, source, ready, finished):
    result = normal_result(rows, source)
    released_baseline(rows, source, ready)
    source_index = next(i for i, (_, line) in enumerate(rows) if line.startswith('M3_BOOT '))
    require(0 <= rows[source_index][0] - rows[0][0] <= .5 and
            all(i > source_index for i, (_, line) in enumerate(rows) if line.startswith('M3_WORK ')),
            'Mismatched fresh source boot')
    require(0 < finished - ready <= 45.25, 'Wrong GPIO action-window duration')
    require(all(a[0] <= b[0] for a, b in zip(rows, rows[1:])), 'Receive time reversed')
    work = []
    for received, line in rows:
        if line.startswith('M3_WORK '):
            match = re.fullmatch(r'M3_WORK sequence=(\d+) monotonic_ms=(\d+) time=(-?\d+) pressed=([01]) device=0', line)
            require(match, 'Invalid GPIO observation')
            work.append((received, int(match[1]), int(match[2]), int(match[4])))
    require(all(b[1] > a[1] and 0 < b[2] - a[2] <= 1250 and
                abs((b[0] - a[0]) * 1000 - (b[2] - a[2])) <= 250
                for a, b in zip(work, work[1:])), 'Discontinuous GPIO observations')
    baseline = [w for w in work if w[0] < ready]
    require(len(baseline) >= 4 and baseline[-1][2] - baseline[0][2] >= 3000 and
            all(w[3] == 0 for w in baseline), 'Released baseline missing')
    action = [w for w in work if ready <= w[0] <= finished]
    require(action, 'No observations in action window')
    groups = []
    for row in action:
        if not groups or groups[-1][0][3] != row[3]:
            groups.append([])
        groups[-1].append(row)
    pressed = [g for g in groups if g[0][3] == 1]
    require(len(pressed) == 5, 'Expected exactly five observed press groups')
    require(all(len(g) >= 2 and g[-1][2] - g[0][2] >= 1000 for g in pressed),
            'Press was not observed across two reports')
    releases = [g for g in groups if g[0][3] == 0]
    # Initial released reports may precede the first press; every inter-press gap is required.
    require(all(len(g) >= 2 and g[-1][2] - g[0][2] >= 1000
                for g in groups[1:-1] if g[0][3] == 0), 'Missing sustained release')
    require(groups[-1][0][3] == 0 and len(groups[-1]) >= 4 and
            groups[-1][-1][2] - groups[-1][0][2] >= 3000, 'Final released state missing')
    require(len(releases) >= 5, 'Missing release after each press')
    result.update(scope='SW8 GPIO5.0 input only; networking/integrated M3 still Pending',
                  presses=5, action_window_s=finished-ready, status='PASS')
    return result


def _capture_gpio(port, directory, source):
    drain(port, directory)
    receiver = Receiver(port, directory)
    receiver.begin()
    started = time.monotonic()
    ready = finished = None
    result = None
    try:
        require(receiver.failure is None, 'GPIO reader failed before reset')
        commander('602003106', 'r\ng\nq\n', directory / 'reset.log')
        boot = None
        while time.monotonic() - started < 10:
            require(receiver.failure is None, 'GPIO reader failed')
            boots = [t for t, line in receiver.rows if line.startswith('M3_BOOT ')]
            if boots:
                boot = boots[0]
                if time.monotonic() - boot >= 5:
                    released_baseline(fresh_rows(list(receiver.rows)), source, time.monotonic())
                    ready = time.monotonic()
                    (directory/'action-window.json').write_text(json.dumps(dict(ready=ready, duration=45)))
                    print('GPIO_READY: five SW8 press3s/release3s pairs now; finish released', flush=True)
                    break
            time.sleep(.02)
        require(ready is not None, 'No valid released baseline')
        while time.monotonic() - ready < 45:
            require(receiver.failure is None, 'GPIO reader failed')
            require(time.monotonic() - started < 60, 'GPIO capture bound exceeded')
            time.sleep(.02)
        finished = time.monotonic()
    finally:
        primary = sys.exc_info()[1]
        try:
            receiver.finish()
        except Exception:
            if primary is None:
                raise
        finally:
            (directory/'lines.json').write_text(json.dumps(receiver.rows,indent=2))
            (directory/'uart.txt').write_text(receiver.text()+'\n')
            (directory/'action-window.json').write_text(json.dumps(dict(ready=ready, finished=finished)))
    require(ready is not None and finished is not None, 'Incomplete action window')
    result = validate_gpio(fresh_rows(receiver.rows), source, ready, finished)
    (directory/'result.json').write_text(json.dumps(result,indent=2))
    return result


def capture_gpio(port, directory, source):
    try:
        return _capture_gpio(port, directory, source)
    except Exception as error:
        try:
            (directory/'failure.txt').write_text(traceback.format_exc())
        except Exception as evidence_error:
            error.evidence_failure = repr(evidence_error)
        raise


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--normal-manifest',type=Path,required=True)
    parser.add_argument('--evidence',type=Path,required=True)
    parser.add_argument('--uart',required=True)
    parser.add_argument('--uart-serial',required=True)
    parser.add_argument('--reset',action='store_true',required=True)
    args=parser.parse_args()
    require(platform.system()=='Darwin','Expected Mac owner')
    verify_jlink_tools()
    image=verify_manifest(args.normal_manifest)
    require(image['profile']=='normal','GPIO trial requires no Shell or fault commands')
    repo=Path(__file__).resolve().parents[2]
    require(not subprocess.check_output(['git','status','--porcelain'],cwd=repo,text=True).strip(),
            'Commit and freeze source before target use')
    args.evidence.mkdir(exist_ok=False)
    inputs=dict(manifest_sha256=sha(args.normal_manifest), firmware=image['source'],
                binary_sha256=sha(image['binary']), runner_commit=subprocess.check_output(
                    ['git','rev-parse','HEAD'],cwd=repo,text=True).strip(),
                runner_sha256=sha(Path(__file__)), uart=args.uart, uart_serial=args.uart_serial,
                stimulus='five manual press3s/release3s pairs; hold released before/after',
                limits='baseline>=3s;45s action;5 presses;>=2 samples/press+gap;final released>=3s',
                operations='verify current BIN; reset/start normal; capture only; no programming/network/watchdog')
    (args.evidence/'inputs.json').write_text(json.dumps(inputs,indent=2))
    import serial
    import serial.tools.list_ports
    selected=select_uart([vars(p) for p in serial.tools.list_ports.comports()],args.uart,args.uart_serial)
    (args.evidence/'serial-inventory.json').write_text(json.dumps(selected,default=str,indent=2))
    with (args.evidence.parent/'evkb-602003106.lock').open('a') as lock:
        fcntl.flock(lock,fcntl.LOCK_EX|fcntl.LOCK_NB)
        check_owners(args.uart,args.evidence/'owners.txt')
        with serial.Serial(args.uart,115200,timeout=.02,exclusive=True) as port:
            fcntl.ioctl(port.fileno(),termios.TIOCEXCL)
            text=commander('602003106',f'verifybin "{image["binary"]}", 0x60000000\nq\n',args.evidence/'verify.log')
            voltage=re.search(r'VTref\s*=\s*([\d.]+)\s*V',text)
            require(voltage and 3<=float(voltage[1])<=3.6 and 'CPUID register: 0x411FC271' in text and
                    'DPIDR: 0x0BD11477' in text and re.search(r'S/N:\s*602003106\b',text) and
                    'Verify successful' in text,'Wrong current firmware/probe/CPU/voltage')
            print(json.dumps(capture_gpio(port,args.evidence,image['source']['git_commit'][:12])),flush=True)


if __name__=='__main__':
    try:
        main()
    except Exception as error:
        print(type(error).__name__+': '+str(error),file=sys.stderr)
        raise
