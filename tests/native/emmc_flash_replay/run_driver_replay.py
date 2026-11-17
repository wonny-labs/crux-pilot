#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Replay MCUboot's trailer sequences through the flash-API driver on native_sim.

The program built from this directory runs the update steps of RAM load with revert through the
flash map and the driver (zephyr/drivers/flash/flash_disk.c) on the eMMC simulator, with a trace
mark after each step. This script starts it on the baseline medium of tests/native/emmc_trailer,
reads the simulator's trace, checks that the driver issued exactly the disk writes and flushes
that the trailer model of docs/evidence/boot-emmc/trailer-replay assumes, and replays power cuts
over the real trace with that record's device and tear models and properties (P1 to P7).

A second build of the program with CONFIG_CRUX_FLASH_DISK_FLUSH_CONTROL=y never flushes. It is a
control: the same replay must fail P3 for it.

Every result is an observation of the simulator's model and of the models in the trailer record.
None of it is evidence about an eMMC device. It makes no eMMC support claim.
"""
import argparse
import json
import os
import shutil
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, '..', 'emmc_trailer'))
sys.path.insert(0, os.path.join(HERE, '..', 'emmc_powercut'))

from replay_model import SECTOR, parse_trace  # noqa: E402
from trailer_model import ModelImage  # noqa: E402
from write_path import (DEVICES, PROPERTIES, LayoutConfig, Trace, baseline_medium,  # noqa: E402
                        replay, run_scenario)

# CONFIG_CRUX_EMMC_SIM_SECTORS of the simulator, which the baseline file has to fill.
SIM_BYTES = 8192 * SECTOR
CONFIG = LayoutConfig.sector()
IMAGE_VERSION = 2
IMAGE_BODY = 3000
PROPS = tuple(PROPERTIES)
# P7 is a property of the tear models, not of the driver: it fails under tears that reach other
# trailer fields, and the trailer record counts it. The driver must hold P1 to P6.
DRIVER_PROPS = ('P1', 'P2', 'P3', 'P4', 'P5', 'P6')
assert DRIVER_PROPS == PROPS[:6], 'the properties of the trailer record changed'
MIN_CASES = 1000  # a replay with a handful of cases would pass for want of a cut
WORKLOADS = (('confirm', False), ('revert', False), ('confirm', True), ('revert', True))


def sim_baseline(config):
    """The baseline medium of the trailer record, grown to the simulator's size."""
    medium = baseline_medium(config)
    if len(medium) > SIM_BYTES:
        raise ValueError('the baseline does not fit the simulator medium')
    return medium + bytes(SIM_BYTES - len(medium))


def workload_name(path, padded):
    return path + ('-pad' if padded else '')


def driver_args(path, padded, config, image_path, trace_path):
    return [f'--emmc-image={image_path}', f'--emmc-trace={trace_path}',
            f'--workload={workload_name(path, padded)}',
            f'--image-hex={ModelImage(IMAGE_VERSION, IMAGE_BODY).encode().hex()}',
            f'--magic-hex={config.layout.magic.hex()}']


def _calls(events):
    """Disk writes made by one driver call: consecutive sector writes with nothing between.
    A flush, a mark or a gap in the sectors ends a call."""
    calls = []
    start = None
    for index, event in enumerate(events + [None]):
        contiguous = (event is not None and event.kind == 'write' and start is not None and
                      event.sector == events[index - 1].sector + 1)
        if start is not None and not contiguous:
            calls.append((start, index, events[start].sector * SECTOR, (index - start) * SECTOR))
            start = None
        if event is not None and event.kind == 'write' and start is None:
            start = index
    return calls


def trace_from_driver(raw, baseline, config, path, padded):
    """The simulator's trace of a driver run, as the trailer record's Trace."""
    events = parse_trace(raw)
    last = 'confirmed' if path == 'confirm' else 'reverted'
    names = ['erased', 'image', 'requested', 'trial', last]
    marks = []
    for index, event in enumerate(events):
        if event.kind == 'mark':
            if event.mark != len(marks) + 1 or event.mark > len(names):
                raise ValueError(f'mark {event.mark} at event {index} is out of order')
            marks.append((names[event.mark - 1], index))
    starts = {names[0]: 1 if events and events[0].kind == 'cache_on' else 0}
    for number, (_, index) in enumerate(marks[:-1] if marks else []):
        starts[names[number + 1]] = index + 1
    return Trace(events, marks, starts, baseline, config, False, path, 'driver', False,
                 _calls(events), padded)


def compare_with_model(driver, path, padded, config):
    """The differences between the driver's trace and the model's, event by event."""
    model = run_scenario(path, config, policy='flush', padded=padded)
    problems = []
    if len(driver.events) != len(model.events):
        problems.append(f'{len(driver.events)} events from the driver, {len(model.events)} in '
                        'the model')
    for index, (got, want) in enumerate(zip(driver.events, model.events)):
        if got != want:
            problems.append(f'event {index}: driver {got.kind} sector {got.sector}, '
                            f'model {want.kind} sector {want.sector}')
            if len(problems) > 5:
                break
    return problems


def evaluate(driver, seeds):
    """The trailer record's replay over the driver's trace, on both device behaviours."""
    outcome = {}
    for device in DEVICES:
        cases = replay(driver, device, seeds=seeds)
        failures = {prop: sum(1 for case in cases if not case.results[prop]) for prop in PROPS}
        outcome[device.name] = {'cases': len(cases), 'failures': failures}
    return outcome


def run_program(exe, path, padded, config, workdir):
    image = os.path.join(workdir, 'medium.img')
    trace = os.path.join(workdir, 'trace.bin')
    with open(image, 'wb') as handle:
        handle.write(sim_baseline(config))
    if os.path.exists(trace):
        os.remove(trace)
    result = subprocess.run([exe] + driver_args(path, padded, config, image, trace),
                            capture_output=True, text=True, timeout=120)
    if result.returncode != 0:
        raise RuntimeError(f'{workload_name(path, padded)} exited {result.returncode}: '
                           f'{result.stdout[-400:]}{result.stderr[-400:]}')
    with open(trace, 'rb') as handle:
        return handle.read()


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument('--exe', required=True, help='the program, built with the driver')
    parser.add_argument('--exe-nofix', required=True,
                        help='the program built with CONFIG_CRUX_FLASH_DISK_FLUSH_CONTROL=y')
    parser.add_argument('--seeds', type=int, default=4)
    parser.add_argument('--out', help='JSON file to write')
    args = parser.parse_args(argv)

    problems = []
    report = {'driver': {}, 'control': {}}
    baseline = sim_baseline(CONFIG)
    workdir = tempfile.mkdtemp(prefix='flash-replay-')
    try:
        for path, padded in WORKLOADS:
            name = workload_name(path, padded)
            raw = run_program(args.exe, path, padded, CONFIG, workdir)
            driver = trace_from_driver(raw, baseline, CONFIG, path, padded)
            differences = compare_with_model(driver, path, padded, CONFIG)
            outcome = evaluate(driver, range(args.seeds))
            if len(driver.marks) != 5:
                problems.append(f'{name}: {len(driver.marks)} marks, not 5')
            for device, entry in outcome.items():
                if entry['cases'] < MIN_CASES:
                    problems.append(f'{name} on {device}: only {entry["cases"]} cases')
            flushes = sum(1 for event in driver.events if event.kind == 'flush')
            report['driver'][name] = {'events': len(driver.events), 'flushes': flushes,
                                      'differences': differences, 'replay': outcome}
            if differences:
                problems.append(f'{name}: the driver trace differs from the model: {differences}')
            for device, entry in outcome.items():
                bad = {p: n for p, n in entry['failures'].items() if n and p in DRIVER_PROPS}
                if bad:
                    problems.append(f'{name} on {device}: properties failed {bad}')

            raw = run_program(args.exe_nofix, path, padded, CONFIG, workdir)
            control = trace_from_driver(raw, baseline, CONFIG, path, padded)
            outcome = evaluate(control, range(args.seeds))
            flushes = sum(1 for event in control.events if event.kind == 'flush')
            report['control'][name] = {'events': len(control.events), 'flushes': flushes,
                                       'replay': outcome}
            if flushes:
                problems.append(f'{name}: the control issued {flushes} flushes')
            if path == 'confirm' and not any(entry['failures']['P3'] for entry in
                                             outcome.values()):
                problems.append(f'{name}: the control did not lose an acknowledged confirm')
            for device, entry in outcome.items():
                if entry['failures']['P1'] or entry['failures']['P2']:
                    problems.append(f'{name} control on {device}: safety failed')
    finally:
        shutil.rmtree(workdir, ignore_errors=True)

    report['problems'] = problems
    if args.out:
        with open(args.out, 'w', encoding='utf-8') as handle:
            json.dump(report, handle, indent=1, sort_keys=True)
            handle.write('\n')
    for name, entry in report['driver'].items():
        counts = {d: sum(v['failures'][p] for p in DRIVER_PROPS)
                  for d, v in entry['replay'].items()}
        print(f"driver  {name:12} events={entry['events']:4} flushes={entry['flushes']:3} "
              f"cases={sum(v['cases'] for v in entry['replay'].values()):6} "
              f"P1-P6 failures={counts}")
    for name, entry in report['control'].items():
        counts = {d: v['failures']['P3'] for d, v in entry['replay'].items()}
        print(f"control {name:12} events={entry['events']:4} flushes={entry['flushes']:3} "
              f"P3 failures={counts}")
    print('all required outcomes hold' if not problems else 'PROBLEMS: ' + '; '.join(problems))
    return 1 if problems else 0


if __name__ == '__main__':
    sys.exit(main())
