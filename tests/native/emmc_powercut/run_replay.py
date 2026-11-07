#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Power-cut replay on the eMMC simulator.

Each workload runs once against the simulator from one provisioned baseline and records a trace.
For each chosen cut position and model, the medium is rebuilt from the baseline and the trace, a
new process mounts it without formatting and checks the invariants, and the result is classified:

  I1 mount  the volume mounts            I4 untouched  files/keep is byte-exact
  I2 data   acknowledged steps hold      I5 recommit   a write, sync and remount succeed
  I3 replace  settings/w2 is one complete generation, not older than acknowledged
  read_error  a file that should exist could not be read, as distinct from a stale value

On FatFs every result is recorded as an observation and only the controls are required, because
FatFs does not claim these invariants. On Reliance Edge (red-fix) every invariant and read_error=0
are asserted on both profiles, and four controls must fail as required. Every result is an
observation of the simulator's model, not evidence about power loss on a device.

Cut positions: on the cached profile, each side of every flush with the lost, prefix and one
reordered draw; then --samples positions per workload and model, drawn by seed, from write
positions only for torn, because only a write at the cut can tear. The no-cache profile has no
flush, so only prefix and torn apply. The sample count alone selects positions,
never the clock; --max-seconds fails a run that takes longer and never skips work. A rebuilt image
that was already verified for the same workload and acknowledged count is not verified again,
because the verifier is deterministic; the JSON counts cases and processes separately. The
verifier runs without a cache, so it reads the rebuilt image as it is.

A W1 case that fails data is verified again with --acked-only, which asks only that every
acknowledged record is intact. The JSON then counts it as I2_acknowledged_intact, when only a
later, unacknowledged record is stale, or as I2_acknowledged_lost. A process that runs longer than
TIMEOUT seconds is killed and recorded as a failure, with its seed, position and image.

Reliance Edge commits as a flush, one whole metaroot, then a flush. Metaroot writes are recognised
by their sectors, and each side of every metaroot sector is a mandatory position under prefix,
torn and reordered, so every metaroot sector is torn. The driver confirms the commit pattern on
each cached trace and stops if it is not found, because the barrier control depends on it. The
position after each mark is tested with lost on the cached profile, which is where an
acknowledged step, including W3's directory steps, must already be durable.

Controls on Reliance Edge, each required to fail:
  flush    red-nofix, cached, lost, W1: data=0 with mount=1 after every mark
  barrier  the red-fix cached trace of W2 (BARRIER_WORKLOAD), reordered, with the flush before
           each metaroot not a barrier and the whole metaroot kept: mount=0, untouched=0 or
           read_error=1 at some position of a barrier window. The same draws with every barrier
           honoured must pass.
  early acknowledgement  red-fix, run-early, cached, lost, W1: data=0 with mount=1 after every
           mark
  damaged image  the baseline with both metaroots overwritten: mount=0

The profile commits when a file is created, so a W3 step that creates a file commits twice and a
cut between the two can leave the new file present and empty. On Reliance Edge the W3 verifier runs
with --create-may-be-empty, which accepts exactly that state in the step after the acknowledged
one. Such cases are counted apart as created_empty, and for each create step one of them is
verified again as a lost acknowledged file and as an empty file at another step, which must fail.
"""
import argparse
import hashlib
import json
import os
import platform
import random
import re
import shutil
import subprocess
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from replay_model import (SECTOR, CommitPatternError, acked_at,  # noqa: E402
                          build_image, flushes_before_metaroot, mandatory_positions,
                          metaroot_sectors, parse_trace)

MEDIUM = 8192 * SECTOR
WORKLOADS = ('W1', 'W2', 'W3')
PROFILES = {
    'cached': [],  # the simulator's default cache size
    'nocache': ['--emmc-cache-kb=0'],
}
PROFILE_MODELS = {
    'cached': ('lost', 'prefix', 'reordered', 'torn'),
    'nocache': ('prefix', 'torn'),
}
FLUSH_MODELS = ('lost', 'prefix', 'reordered')
METAROOT_MODELS = ('prefix', 'torn', 'reordered')
FIELDS = ('mount', 'data', 'read_error', 'replace', 'untouched', 'recommit')
VIOLATIONS = {'mount': 'I1', 'data': 'I2', 'replace': 'I3', 'untouched': 'I4', 'recommit': 'I5'}
LINE = re.compile(r'POWERCUT ' + ' '.join(f'{field}=([01])' for field in FIELDS) +
                  r'(?: created_empty=([01]))?')
TIMEOUT = 60
# On W1 the other copy of the file's inode is a valid older inode and file data has no check, so a
# draw that keeps the new metaroot only gives data=0, which an older state explains: the barrier
# control cannot fail there. W2's windows hold directory and new inode blocks, and it does.
BARRIER_WORKLOAD = 'W2'
# W3 steps that create a file, as in workloads.c. Reliance Edge commits the create, so a cut inside
# such a step can leave the new file present and empty; the verifier accepts that state only with
# --create-may-be-empty, and only in the step after the acknowledged one.
W3_CREATE_STEPS = {2: 'files/w3b', 4: 'files/w3dir/e', 6: 'files/w3c', 11: 'files/w3d'}
# The W3 files and their indices in workloads.c, for --phase=stage --empty=INDEX.
W3_FILES = {'files/w3a': 0, 'files/w3b': 1, 'files/w3c': 2, 'files/w3d': 3, 'files/w3dir/e': 5}
W3_STEPS = 12
IMAGES = {'fatfs': {'fix': 'fat-fix', 'nofix': 'fat-nofix'},
          'reliance': {'fix': 'red-fix', 'nofix': 'red-nofix'}}


def derive(*parts):
    """A seed for one draw, stable across runs and Python versions."""
    text = ':'.join(str(part) for part in parts).encode()
    return int.from_bytes(hashlib.sha256(text).digest()[:8], 'little')


def violations(result):
    found = [invariant for field, invariant in VIOLATIONS.items() if result[field] == 0]
    if result['read_error']:
        found.append('read_error')
    return found


class RunError(RuntimeError):
    pass


class Replay:
    def __init__(self, args):
        self.args = args
        self.exes = {'fix': args.exe_fix, 'nofix': args.exe_nofix}
        self.work = os.path.join(args.out, f'work-{args.fs}')
        self.kept = os.path.join(args.out, f'failed-{args.fs}')
        self.image = os.path.join(self.work, 'medium.bin')
        self.reliance = args.fs == 'reliance'
        self.names = IMAGES[args.fs]
        self.metaroot = metaroot_sectors() if self.reliance else frozenset()
        self.traces = {}
        self.created_empty = []
        self.memo = {}
        self.cases = 0
        self.processes = 0
        self.failures = []

    def run(self, exe, *options):
        """Returns (exit code, stdout lines, stderr); the code is None for a process that hung."""
        try:
            proc = subprocess.run([self.exes[exe], *options], capture_output=True, text=True,
                                  timeout=TIMEOUT)
        except subprocess.TimeoutExpired:
            return None, [], f'killed after {TIMEOUT} s'
        return proc.returncode, proc.stdout.splitlines(), proc.stderr

    def provision(self):
        os.makedirs(self.work, exist_ok=True)
        shutil.rmtree(self.kept, ignore_errors=True)  # images of an earlier run's failures
        path = os.path.join(self.work, 'baseline.bin')
        if os.path.exists(path):
            os.remove(path)  # the simulator loads an existing image, and this one must be new
        rc, out, err = self.run('fix', f'--emmc-image={path}', '--emmc-cache-kb=0',
                                '--phase=provision')
        if rc != 0 or out != ['PROVISIONED']:
            raise RunError(f'provision failed: {rc} {out} {err}')
        with open(path, 'rb') as handle:
            self.baseline = handle.read()
        if len(self.baseline) != MEDIUM:
            raise RunError(f'the baseline has {len(self.baseline)} bytes')

    def trace(self, exe, workload, profile, phase='run'):
        key = (exe, workload, profile, phase)
        if key not in self.traces:
            self.traces[key] = self.record_trace(exe, workload, profile, phase)
        return self.traces[key]

    def record_trace(self, exe, workload, profile, phase):
        run_image = os.path.join(self.work, 'run.bin')
        path = os.path.join(self.work, f'{exe}-{workload}-{profile}-{phase}.trace')
        shutil.copyfile(os.path.join(self.work, 'baseline.bin'), run_image)
        rc, out, err = self.run(exe, f'--emmc-image={run_image}', f'--emmc-trace={path}',
                                *PROFILES[profile], f'--phase={phase}', f'--workload={workload}')
        if rc != 0 or out != ['RAN']:
            raise RunError(f'{exe} {workload} {profile} {phase} failed: {rc} {out} {err}')
        with open(path, 'rb') as handle:
            return parse_trace(handle.read())

    def verify(self, exe, image, workload, acked, acked_only=False):
        """Returns the six fields and None, or None and why the verifier gave no POWERCUT line.

        acked_only narrows the W1 data check to the acknowledged records; such a call is a second
        look at a case, not a case of its own.
        """
        if not acked_only:
            self.cases += 1
        create_empty = self.reliance and workload == 'W3'
        key = (exe, workload, acked, acked_only, hashlib.sha256(image).digest())
        if key in self.memo:
            return self.memo[key]
        with open(self.image, 'wb') as handle:
            handle.write(image)
        rc, out, err = self.run(exe, f'--emmc-image={self.image}', '--emmc-cache-kb=0',
                                '--phase=verify', f'--workload={workload}', f'--acked={acked}',
                                *(['--acked-only'] if acked_only else []),
                                *(['--create-may-be-empty'] if create_empty else []))
        self.processes += 1
        lines = [LINE.fullmatch(line) for line in out]
        if (rc != 0 or len(out) != 1 or lines[0] is None or
                (lines[0].group(len(FIELDS) + 1) is not None) != create_empty):
            answer = (None, f'rc {rc} {out} {err[-500:]}')
            print(f'verifier gave no result: {answer[1]}', file=sys.stderr)
        else:
            result = {field: int(value) for field, value in zip(FIELDS, lines[0].groups())}
            if create_empty:
                result['created_empty'] = int(lines[0].group(len(FIELDS) + 1))
            answer = (result, None)
        self.memo[key] = answer
        return answer

    def keep(self, name, image):
        os.makedirs(self.kept, exist_ok=True)
        path = os.path.join(self.kept, f'{name}.bin')
        with open(path, 'wb') as handle:
            handle.write(image)
        return os.path.relpath(path, self.args.out)

    def fail(self, case, reason, image):
        case = dict(case, reason=reason)
        case['image'] = self.keep(f'case-{len(self.failures)}', image)
        self.failures.append(case)
        print(f'FAIL {reason}: {json.dumps(case, sort_keys=True)}')

    def case(self, exe, events, workload, profile, model, position):
        seed = derive(self.args.seed, exe, workload, profile, model, position)
        image = build_image(self.baseline, events, position, model, seed)
        acked = acked_at(events, position)
        record = {'exe': exe, 'workload': workload, 'profile': profile, 'model': model,
                  'position': position, 'seed': seed, 'acked': acked}
        result, reason = self.verify(exe, image, workload, acked)
        if result is None:
            record['no_result'] = reason
        return record, image, result

    def positions(self, events, workload, profile):
        """Distinct (position, model) pairs: the mandatory ones first, then the sampled ones."""
        chosen = []
        if profile == 'cached':
            for position in mandatory_positions(events):
                chosen += [(position, model) for model in FLUSH_MODELS]
        if profile == 'cached' and self.reliance:
            chosen += [(index + 1, 'lost') for index, event in enumerate(events)
                       if event.kind == 'mark']
        models = METAROOT_MODELS if profile == 'cached' else PROFILE_MODELS[profile]
        for position in self.metaroot_positions(events):
            chosen += [(position, model) for model in models]
        writes = [index for index, event in enumerate(events) if event.kind == 'write']
        for model in PROFILE_MODELS[profile]:
            draw = random.Random(derive(self.args.seed, workload, profile, model, 'positions'))
            # torn tears the write at the cut, so only a write position tears anything
            population = writes if model == 'torn' else range(len(events) + 1)
            sample = draw.sample(population, min(self.args.samples, len(population)))
            chosen += [(position, model) for position in sorted(sample)]
        return list(dict.fromkeys(chosen))  # a drawn position can also be a mandatory one

    def metaroot_positions(self, events):
        """Each side of every metaroot sector write. FatFs has no metaroot."""
        return sorted({position for index, event in enumerate(events)
                       if event.kind == 'write' and event.sector in self.metaroot
                       for position in (index, index + 1)})

    def commits(self, events, workload):
        """The flush before each metaroot write of a cached Reliance Edge trace, checked."""
        try:
            found = flushes_before_metaroot(events)
        except CommitPatternError as error:
            raise RunError(f'{workload} cached: unexpected commit pattern: {error}') from error
        if not found:
            raise RunError(f'{workload} cached: no commit (flush, metaroot, flush) in the trace')
        return found

    def sanity(self, events, workload, profile, report):
        """The verifier passes on the baseline and on the complete run."""
        for position in (0, len(events)):
            record, image, result = self.case('fix', events, workload, profile, 'prefix',
                                              position)
            ok = result is not None and not violations(result)
            report.append(dict(record, result=result, passed=ok))
            if not ok:
                self.fail(record, 'the verifier does not pass on an uncut image', image)

    def observe(self, events, workload, profile):
        groups = {}
        failed = []
        for position, model in self.positions(events, workload, profile):
            record, image, result = self.case('fix', events, workload, profile, model, position)
            group = groups.setdefault(model, {'cases': 0, 'passed': 0, 'no_result': 0,
                                              'violations': {}})
            group['cases'] += 1
            if model == 'torn':
                tore = position < len(events) and events[position].kind == 'write'
                group['tore'] = group.get('tore', 0) + tore
            if result is None:
                group['no_result'] += 1
                self.fail(record, 'the verifier gave no result', image)
                continue
            found = violations(result)
            if result.get('created_empty'):
                self.created_empty.append(dict(record, result=result))
                group['created_empty'] = group.get('created_empty', 0) + 1
            if not found:
                group['passed'] += 1
                continue
            for invariant in found:
                group['violations'][invariant] = group['violations'].get(invariant, 0) + 1
            if workload == 'W1' and result['data'] == 0:
                intact = self.acknowledged_intact(image, record)
                record['acknowledged_intact'] = intact
                key = 'I2_acknowledged_intact' if intact else 'I2_acknowledged_lost'
                group[key] = group.get(key, 0) + 1
            failed.append(dict(record, result=result))
            if self.reliance:
                self.fail(dict(record, result=result),
                          f'red-fix violates {" ".join(found)}', image)
        return groups, failed

    def acknowledged_intact(self, image, record):
        """Whether every acknowledged W1 record is intact, so that only a later step is stale."""
        result, reason = self.verify(record['exe'], image, 'W1', record['acked'], acked_only=True)
        if result is None:
            self.fail(record, f'the acked-only verifier gave no result: {reason}', image)
            return False
        return result['data'] == 1

    def flush_control(self):
        """The nofix image, cached, lost: W1 must lose acknowledged data after each mark."""
        events = self.trace('nofix', 'W1', 'cached')
        cases = []
        failed = False
        marks = [index + 1 for index, event in enumerate(events) if event.kind == 'mark']
        for position in marks:
            record, image, result = self.case('nofix', events, 'W1', 'cached', 'lost', position)
            ok = result is not None and result['mount'] == 1 and result['data'] == 0
            cases.append(dict(record, result=result, failed_as_required=ok))
            if not ok:
                failed = True
                self.fail(record, 'the flush control did not report data=0 with mount=1', image)
        return {
            'name': 'flush', 'image': self.names['nofix'], 'profile': 'cached', 'model': 'lost',
            'workload': 'W1', 'required': 'mount=1 and data=0 at every position after a mark',
            'flushes_in_trace': sum(1 for event in events if event.kind == 'flush'),
            'cases': cases, 'failed_as_required': bool(marks) and not failed,
        }

    def early_ack_observation(self):
        """fat-fix, cached, lost, W1 with each mark before its sync: recorded, not asserted."""
        events = self.trace('fix', 'W1', 'cached', phase='run-early')
        cases = []
        for position in [index + 1 for index, event in enumerate(events) if event.kind == 'mark']:
            record, image, result = self.case('fix', events, 'W1', 'cached', 'lost', position)
            record['phase'] = 'run-early'
            if result is None:
                self.fail(record, 'the verifier gave no result', image)
            cases.append(dict(record, result=result))
        return {
            'image': 'fat-fix', 'phase': 'run-early', 'profile': 'cached', 'model': 'lost',
            'workload': 'W1', 'positions': 'after each mark', 'cases': cases,
            'data_failures': sum(1 for case in cases
                                 if case['result'] is not None and case['result']['data'] == 0),
        }

    def early_ack_control(self):
        """red-fix, run-early, cached, lost, W1: each mark precedes its sync, so data=0 after it."""
        events = self.trace('fix', 'W1', 'cached', phase='run-early')
        cases = []
        failed = False
        marks = [index + 1 for index, event in enumerate(events) if event.kind == 'mark']
        for position in marks:
            record, image, result = self.case('fix', events, 'W1', 'cached', 'lost', position)
            record['phase'] = 'run-early'
            ok = result is not None and result['mount'] == 1 and result['data'] == 0
            cases.append(dict(record, result=result, failed_as_required=ok))
            if not ok:
                failed = True
                self.fail(record, 'the early acknowledgement control did not report data=0 '
                          'with mount=1', image)
        return {
            'name': 'early acknowledgement', 'image': 'red-fix', 'phase': 'run-early',
            'profile': 'cached', 'model': 'lost', 'workload': 'W1',
            'required': 'mount=1 and data=0 at every position after a mark',
            'cases': cases, 'failed_as_required': bool(marks) and not failed,
        }

    def barrier_control(self, workload=BARRIER_WORKLOAD, required=True):
        """red-fix, cached, reordered with the flush before each metaroot not a barrier.

        Each position from that flush to the flush after the metaroot is drawn once, keeping
        every metaroot sector and keeping each other sector of the window with even odds. A
        result that an older state cannot explain is required somewhere: mount=0, untouched=0 or
        read_error=1; data=0 alone does not count. The same draw with every flush a barrier is a
        red-fix case and must pass, so that the failure comes from the missing barrier. With
        required false, the same draw is recorded as an observation on another workload.
        """
        events = self.trace('fix', workload, 'cached')
        ignored = self.commits(events, workload)
        flushes = [index for index, event in enumerate(events) if event.kind == 'flush']
        cases = []
        hits = 0
        for before in sorted(ignored):
            after = next(index for index in flushes if index > before)
            for position in range(before + 1, after + 1):
                seed = derive(self.args.seed, 'barrier', workload, position)
                acked = acked_at(events, position)
                record = {'exe': 'fix', 'workload': workload, 'profile': 'cached',
                          'model': 'reordered', 'position': position, 'seed': seed,
                          'acked': acked, 'window': [before, after]}
                image = build_image(self.baseline, events, position, 'reordered', seed,
                                    ignored_flushes=ignored, keep_sectors=self.metaroot)
                result, reason = self.verify('fix', image, workload, acked)
                hit = result is not None and (result['mount'] == 0 or result['untouched'] == 0
                                              or result['read_error'] == 1)
                hits += hit
                honoured = build_image(self.baseline, events, position, 'reordered', seed,
                                       keep_sectors=self.metaroot)
                check, check_reason = self.verify('fix', honoured, workload, acked)
                passed = check is not None and not violations(check)
                cases.append(dict(record, result=result, no_result=reason, unexplained=hit,
                                  honoured=check, honoured_passed=passed))
                if result is None:
                    self.fail(record, 'the barrier control verifier gave no result', image)
                if not passed:
                    self.fail(dict(record, result=check, no_result=check_reason),
                              'red-fix fails the barrier draw with every barrier honoured',
                              honoured)
        if required and not hits:
            self.failures.append({'reason': 'the barrier control did not report mount=0, '
                                            'untouched=0 or read_error=1 at any position'})
            print('FAIL the barrier control found no result that an old state cannot explain')
        return {
            'name': 'barrier', 'image': 'red-fix', 'profile': 'cached', 'model': 'reordered',
            'workload': workload, 'ignored_flushes': sorted(ignored),
            'keep_sectors': [min(self.metaroot), max(self.metaroot)],
            'required': 'mount=0, untouched=0 or read_error=1 at some position of a window',
            'positions': len(cases), 'unexplained': hits,
            'unexplained_by_field': {
                field: sum(1 for case in cases if case['result'] is not None and
                           case['result'][field] == value)
                for field, value in (('mount', 0), ('untouched', 0), ('read_error', 1),
                                     ('data', 0))},
            'cases': cases, 'failed_as_required': hits > 0,
        } if required else {
            'workload': workload, 'positions': len(cases), 'unexplained': hits,
            'unexplained_by_field': {
                field: sum(1 for case in cases if case['result'] is not None and
                           case['result'][field] == value)
                for field, value in (('mount', 0), ('untouched', 0), ('read_error', 1),
                                     ('data', 0))},
            'honoured_passed': all(case['honoured_passed'] for case in cases),
        }

    def created_empty_report(self):
        """The W3 cases that passed only as a created, still empty file, and checks that can fail.

        Each such case must lie in a step that creates a file. For each create step, the first such
        image is verified again as if that step had been acknowledged, where the empty file is a
        lost acknowledged file, and as if each other create step were in progress, where the empty
        file is not the one that step creates. Each must report data=0.
        """
        by_step = {}
        for case in self.created_empty:
            step = case['acked'] + 1
            by_step.setdefault(step, []).append(case)
            if step not in W3_CREATE_STEPS:
                self.fail(case, f'a created, empty file was accepted in step {step}, which '
                          'creates no file', build_image(self.baseline,
                                                         self.trace('fix', 'W3', case['profile']),
                                                         case['position'], case['model'],
                                                         case['seed']))
        checks = []
        for step, cases in sorted(by_step.items()):
            case = cases[0]
            events = self.trace('fix', 'W3', case['profile'])
            image = build_image(self.baseline, events, case['position'], case['model'],
                                case['seed'])
            claims = [(step, 'lost acknowledged file')]
            claims += [(other - 1, 'empty file at another step')
                       for other in sorted(W3_CREATE_STEPS) if other != step]
            for claim, meaning in claims:
                result, reason = self.verify('fix', image, 'W3', claim)
                ok = (result is not None and result['data'] == 0 and
                      result['created_empty'] == 0)
                check = dict(case, claimed_acked=claim, meaning=meaning, result=result,
                             no_result=reason, failed_as_required=ok)
                checks.append(check)
                if not ok:
                    self.fail(check, f'the W3 check accepted a {meaning}', image)
        staged = self.staged_w3_checks()
        counts = {}
        for case in self.created_empty:
            key = f'{case["profile"]} {case["model"]} step {case["acked"] + 1}'
            counts[key] = counts.get(key, 0) + 1
        return {
            'cases': len(self.created_empty),
            'steps': {str(step): {'path': W3_CREATE_STEPS.get(step), 'cases': len(cases)}
                      for step, cases in sorted(by_step.items())},
            'by_profile_model_step': dict(sorted(counts.items())),
            'acknowledged_intact': all(case['result']['data'] == 1 and
                                       not violations(case['result'])
                                       for case in self.created_empty),
            'checks': checks,
            'checks_failed_as_required': bool(checks) and all(c['failed_as_required']
                                                              for c in checks),
            'staged': staged,
        }

    def staged_w3_checks(self):
        """The W3 check against states made on purpose, not by a cut.

        For each acknowledged count n, red-fix makes the state after n steps, once as it is and
        once with each W3 file left with size 0. The check must accept the state as it is, and an
        empty file only when step n + 1 creates that file; any other empty file is a lost or
        unexpected one and must give data=0.
        """
        stage_image = os.path.join(self.work, 'stage.bin')
        cases = []
        for acked in range(W3_STEPS):
            for path in [None, *W3_FILES]:
                shutil.copyfile(os.path.join(self.work, 'baseline.bin'), stage_image)
                options = [] if path is None else [f'--empty={W3_FILES[path]}']
                rc, out, err = self.run('fix', f'--emmc-image={stage_image}', '--emmc-cache-kb=0',
                                        '--phase=stage', '--workload=W3', f'--acked={acked}',
                                        *options)
                if out == ['STAGE-SKIP']:
                    continue
                if rc != 0 or out != ['STAGED']:
                    raise RunError(f'stage {acked} {path} failed: {rc} {out} {err}')
                with open(stage_image, 'rb') as handle:
                    image = handle.read()
                result, reason = self.verify('fix', image, 'W3', acked)
                accept = path is None or W3_CREATE_STEPS.get(acked + 1) == path
                if result is None:
                    ok = False
                elif accept:
                    ok = (not violations(result) and
                          result['created_empty'] == (path is not None))
                else:
                    ok = result['data'] == 0 and result['created_empty'] == 0
                case = {'acked': acked, 'empty': path, 'accept': accept, 'result': result,
                        'no_result': reason, 'as_required': ok}
                cases.append(case)
                if not ok:
                    self.fail(case, 'the W3 check did not treat a staged state as required',
                              image)
        return {
            'cases': len(cases),
            'accepted_empty': sum(1 for c in cases if c['accept'] and c['empty'] is not None),
            'refused_empty': sum(1 for c in cases if not c['accept']),
            'as_required': bool(cases) and all(c['as_required'] for c in cases),
            'detail': cases,
        }

    def damaged_metaroot_control(self):
        """red-fix, the baseline with both metaroots overwritten, must not mount.

        Each metaroot overwritten alone is recorded too: the file system keeps two, so one alone
        is expected to leave a volume that mounts, which shows that the damage hit the metaroots.
        """
        blocks = sorted(self.metaroot)
        half = len(blocks) // 2
        regions = {'first metaroot': blocks[:half], 'second metaroot': blocks[half:],
                   'both metaroots': blocks}
        for name in ('first metaroot', 'second metaroot'):
            sectors = regions[name]
            if self.baseline[sectors[0] * SECTOR:(sectors[-1] + 1) * SECTOR].strip(b'\x00') == b'':
                raise RunError(f'the {name} of the baseline is blank')
        results = {}
        for name, sectors in regions.items():
            image = bytearray(self.baseline)
            for sector in sectors:
                image[sector * SECTOR:(sector + 1) * SECTOR] = b'\xa5' * SECTOR
            image = bytes(image)
            result, reason = self.verify('fix', image, 'W1', 0)
            record = {'exe': 'fix', 'workload': 'W1', 'acked': 0,
                      'damage': f'sectors {sectors[0]} to {sectors[-1]} = 0xa5'}
            if result is None:
                record['no_result'] = reason
            results[name] = (dict(record, result=result), image)
        record, image = results['both metaroots']
        ok = record['result'] is not None and record['result']['mount'] == 0
        if not ok:
            self.fail(record, 'the damaged image control did not report mount=0', image)
        return {'name': 'damaged image', 'image': 'red-fix', 'required': 'mount=0',
                'case': record,
                'single_metaroot_observations': {name: results[name][0]
                                                 for name in ('first metaroot',
                                                              'second metaroot')},
                'failed_as_required': ok}

    def damaged_control(self):
        """fat-fix, the baseline with its boot sector overwritten, must not mount."""
        boot = self.baseline[:SECTOR]
        if boot[510:512] != b'\x55\xaa' or boot[0] not in (0xeb, 0xe9):
            raise RunError('sector 0 of the baseline is not a FAT boot sector')
        image = b'\xa5' * SECTOR + self.baseline[SECTOR:]
        result, reason = self.verify('fix', image, 'W1', 0)
        ok = result is not None and result['mount'] == 0
        record = {'exe': 'fix', 'workload': 'W1', 'acked': 0, 'damage': 'sector 0 = 0xa5'}
        if result is None:
            record['no_result'] = reason
        if not ok:
            self.fail(record, 'the damaged image control did not report mount=0', image)
        return {'name': 'damaged image', 'image': 'fat-fix', 'required': 'mount=0',
                'case': dict(record, result=result), 'failed_as_required': ok}

    def execute(self):
        started = time.monotonic()
        self.provision()
        report = {'fs': self.args.fs, 'seed': self.args.seed, 'samples': self.args.samples,
                  'medium_bytes': MEDIUM, 'asserted': [], 'traces': {}, 'sanity': [],
                  'observations': {}, 'observed_failures': [], 'controls': []}
        for workload in WORKLOADS:
            for profile in PROFILES:
                events = self.trace('fix', workload, profile)
                info = report['traces'].setdefault(workload, {})[profile] = {
                    'events': len(events),
                    'writes': sum(1 for e in events if e.kind == 'write'),
                    'flushes': sum(1 for e in events if e.kind == 'flush'),
                    'marks': sum(1 for e in events if e.kind == 'mark'),
                    'flush_positions': len(mandatory_positions(events)),
                    'metaroot_positions': len(self.metaroot_positions(events)),
                }
                if self.reliance and profile == 'cached':
                    info['commits'] = len(self.commits(events, workload))
                self.sanity(events, workload, profile, report['sanity'])
                groups, failed = self.observe(events, workload, profile)
                report['observations'].setdefault(workload, {})[profile] = groups
                report['observed_failures'] += failed
        if self.reliance:
            report['asserted'] = ['I1', 'I2', 'I3', 'I4', 'I5', 'read_error=0']
            report['controls'] = [self.flush_control(), self.barrier_control(),
                                  self.early_ack_control(), self.damaged_metaroot_control()]
            report['barrier_observations'] = [
                self.barrier_control(workload, required=False)
                for workload in WORKLOADS if workload != BARRIER_WORKLOAD]
            report['created_empty'] = self.created_empty_report()
        else:
            report['controls'] = [self.flush_control(), self.damaged_control()]
            report['early_ack_observation'] = self.early_ack_observation()
        seconds = time.monotonic() - started
        if seconds > self.args.max_seconds:
            self.failures.append({'reason': f'took {seconds:.1f} s, over --max-seconds '
                                            f'{self.args.max_seconds}'})
        report['counts'] = {'cases': self.cases, 'processes': self.processes,
                            'observed_failures': len(report['observed_failures'])}
        report['failures'] = self.failures
        report['timing'] = {'seconds': round(seconds, 1), 'max_seconds': self.args.max_seconds,
                            'machine': platform.machine()}
        return report


def summarise(report):
    for workload, profiles in report['observations'].items():
        for profile, groups in profiles.items():
            for model, group in groups.items():
                found = ' '.join(f'{k}={v}' for k, v in sorted(group['violations'].items()))
                for key in ('tore', 'I2_acknowledged_intact', 'I2_acknowledged_lost',
                            'created_empty'):
                    if key in group:
                        found += f' {key}={group[key]}'
                print(f'{workload} {profile:7} {model:9} cases {group["cases"]:4} '
                      f'passed {group["passed"]:4} {found}')
    early = report.get('early_ack_observation')
    if early is not None:
        print(f'run-early W1 lost: {len(early["cases"])} cases, '
              f'data=0 in {early["data_failures"]}')
    for control in report['controls']:
        print(f'control {control["name"]}: '
              f'{"failed as required" if control["failed_as_required"] else "DID NOT FAIL"}')
    for observed in report.get('barrier_observations', []):
        print(f'barrier draw on {observed["workload"]}, recorded: {observed["positions"]} '
              f'positions, {observed["unexplained"]} unexplained, '
              f'{observed["unexplained_by_field"]}')
    created = report.get('created_empty')
    if created is not None:
        print(f'W3 created and still empty: {created["cases"]} cases in steps '
              f'{sorted(int(step) for step in created["steps"])}, acknowledged data intact: '
              f'{created["acknowledged_intact"]}; narrowed check '
              f'{"failed as required" if created["checks_failed_as_required"] else "DID NOT FAIL"}'
              f' in {len(created["checks"])} checks; staged states '
              f'{created["staged"]["cases"]} ({created["staged"]["accepted_empty"]} empty files '
              f'accepted, {created["staged"]["refused_empty"]} refused), '
              f'{"as required" if created["staged"]["as_required"] else "NOT AS REQUIRED"}')
    counts = report['counts']
    print(f'{counts["cases"]} cases, {counts["processes"]} verifier processes, '
          f'{report["timing"]["seconds"]} s, seed {report["seed"]}')


def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--exe-fix', required=True,
                        help='image built with the flush correction (fat-fix or red-fix)')
    parser.add_argument('--exe-nofix', required=True,
                        help='image built without it (fat-nofix or red-nofix)')
    parser.add_argument('--fs', required=True, choices=('fatfs', 'reliance'))
    parser.add_argument('--seed', type=int, required=True)
    parser.add_argument('--samples', type=int, required=True,
                        help='sampled positions per workload, profile and model')
    parser.add_argument('--max-seconds', type=float, required=True)
    parser.add_argument('--out', required=True, help='directory for the JSON and kept images')
    args = parser.parse_args()
    if args.samples < 0:
        parser.error('--samples must not be negative')

    os.makedirs(args.out, exist_ok=True)
    replay = Replay(args)
    try:
        report = replay.execute()
    except RunError as error:
        print(f'FAIL {error}', file=sys.stderr)
        report = {'fs': args.fs, 'seed': args.seed, 'samples': args.samples,
                  'error': str(error), 'failures': replay.failures}
    with open(os.path.join(args.out, f'{args.fs}.json'), 'w') as handle:
        json.dump(report, handle, indent=1, sort_keys=True)
        handle.write('\n')
    if 'error' in report:
        return 1
    summarise(report)
    bad_controls = [c['name'] for c in report['controls'] if not c['failed_as_required']]
    if 'created_empty' in report and not (report['created_empty']['checks_failed_as_required'] and
                                          report['created_empty']['staged']['as_required']):
        bad_controls.append('narrowed W3 check')
    if report['failures'] or bad_controls:
        print(f'FAIL: {len(report["failures"])} failures; controls that did not fail: '
              f'{bad_controls}', file=sys.stderr)
        return 1
    return 0


if __name__ == '__main__':
    sys.exit(main())
