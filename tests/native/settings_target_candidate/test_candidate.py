#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Controlled-copy direct Settings path checks; no upstream/media modifications."""
import argparse
import errno
import hashlib
import json
import os
from pathlib import Path
import shutil
import tempfile
from support import prepare, nvs_headers, run

HERE = Path(__file__).resolve().parent
EIO, BAD = -errno.EIO, -errno.EBADMSG
# rc, deletes, writes, last_name_id, lock depth; literal independently derived effects.
CASES = {
    'read_valid': (0,0,0,32769,0),
    'read_hard_name': (EIO,0,0,32769,0),
    'read_hard_value': (EIO,0,0,32769,0),
    'read_orphan_name': (BAD,0,0,32769,0),
    'read_orphan_value': (BAD,0,0,32769,0),
    'read_hole': (0,0,0,32769,0),
    'read_bad_name': (BAD,0,0,32769,0),
    'read_long_name': (BAD,0,0,32769,0),
    'read_trailing_nul': (0,0,0,32769,0),
    'read_corrupt_value': (EIO,0,0,32769,0),
    'read_short_value': (EIO,0,0,32769,0),
    'save_valid': (0,0,1,32769,0),
    'save_zero': (0,0,1,32769,0),
    'save_later_hard_name': (EIO,0,0,32770,0),
    'save_hard_name': (EIO,0,0,32769,0),
    'save_hard_value': (EIO,0,0,32769,0),
    'save_orphan_name': (BAD,0,0,32769,0),
    'save_orphan_value': (BAD,0,0,32769,0),
    'save_bad_name': (BAD,0,0,32769,0),
    'save_long_name': (BAD,0,0,32769,0),
    'save_hole': (0,0,2,32769,0),
    'save_new': (0,0,3,32769,0),
    'save_new_write_error': (EIO,0,1,32768,0),
    'save_new_value_write_error': (EIO,0,2,32769,0),
    'save_new_name_write_error': (EIO,0,3,32769,0),
    'delete_valid': (0,2,1,32768,0),
    'delete_hole': (0,0,0,32769,0),
    'delete_error': (EIO,1,0,32769,0),
    'delete_second_error': (EIO,2,0,32769,0),
    'delete_count_error': (EIO,2,1,32769,0),
    'init_valid': (0,0,0,32769,0),
    'init_count_missing': (BAD,0,0,32769,0),
    'init_count_error': (EIO,0,0,32769,0),
    'init_count_short': (BAD,0,0,32769,0),
    'init_count_range': (BAD,0,0,32769,0),
    'init_mount_error': (EIO,0,0,32769,0),
}
DIRECT = {
    'direct_scan_error': f'{EIO} 1 0 0 0',
    'earlier_error_two': f'{EIO} 1 0 0 0',
    'later_error': f'{EIO} 2 0 0 0',
    'positive_stop': '7 1 0 0 0',
    'negative_stop': f'{-errno.ENOSPC} 1 0 0 0',
    'valid_two': '0 2 0 0 0',
}

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--zephyr', type=Path, required=True)
    parser.add_argument('--variant', choices=('original','candidate'), required=True)
    parser.add_argument('--report', type=Path, required=True)
    args = parser.parse_args()
    observations = []
    with tempfile.TemporaryDirectory(prefix='crux-target-copy-') as tmp:
        work = Path(tmp)
        hashes = prepare(args.zephyr, work, args.variant == 'candidate')
        nvs_headers(work)
        cc = os.environ.get('CC') or shutil.which('cc')
        command = [cc, '-std=c17', '-Wall', '-Wextra', '-Wpedantic', '-Werror',
                   '-fsanitize=address,undefined', '-fno-omit-frame-pointer', '-g',
                   '-DCONFIG_SETTINGS_NVS_NAME_CACHE=0', '-DCONFIG_NVS_DATA_CRC=1',
                   '-I', str(work), '-I', str(work/'subsys/settings/include')]
        for binary, cases in [('backend', CASES), ('direct', DIRECT)]:
            compiled = run([*command, str(HERE/(binary+'.c')), '-o', str(work/binary)])
            if compiled.returncode:
                raise RuntimeError(compiled.stdout+compiled.stderr)
            for case, expect in cases.items():
                expect = expect if isinstance(expect, str) else ' '.join(map(str,expect))
                result = run([str(work/binary),case])
                passed = result.returncode == 0 and result.stdout.strip() == expect and not result.stderr
                observations.append(dict(binary=binary,case=case,expected=expect,actual=result.stdout.strip(),
                                         returncode=result.returncode,stderr=result.stderr,passed=passed))
                print(('PASS' if passed else 'FAIL')+' '+case+' '+result.stdout.strip())
        unchanged = all(hashlib.sha256((args.zephyr/rel).read_bytes()).hexdigest() == sha for rel,sha in hashes.items())
        report = dict(variant=args.variant,source_sha256=hashes,pinned_inputs_unchanged=unchanged,
                      compiler=run([cc,'--version']).stdout.splitlines()[0],flags=command[1:],
                      fixture_sha256={p.name:hashlib.sha256(p.read_bytes()).hexdigest() for p in HERE.iterdir()
                                      if p.suffix in ('.py','.c','.patch')},cases=observations)
        args.report.write_text(json.dumps(report,indent=2)+'\n')
        if not unchanged: raise RuntimeError('Upstream input changed')
    return 0 if all(o['passed'] for o in observations) else 1

if __name__ == '__main__':
    raise SystemExit(main())
