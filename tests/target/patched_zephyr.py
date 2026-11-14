"""Freeze the identity of the patched Zephyr copy of the bench image, and check the frozen record (design B2, option 2).

Design: docs/rt1060-bench-freeze-and-capture-design.md, part 1. The bench image is built with ``ZEPHYR_BASE`` pointing at a
copy of the pinned Zephyr tree to which an ordered LIST of patches was applied. The copy is not a west project, so the build
provenance does not see it. Two steps record it around the build, and one check reads only what they froze:

* ``capture``  before the build: the marker text and the tree digest ``crux-zephyr-tree-v1`` of the copy.
* ``freeze``   after the build: the digest again, the patches, the touched files, the marker, a re-derivation of the tree from
  the stock checkout, and the compile-commands check; it copies what it names into ``<build>/patched-zephyr/`` and writes
  ``patched-zephyr.json``.
* ``check_record``  used by ``bench_image.check_image``: it reads the frozen manifest and the frozen record and artefacts only.
  It never reads the copy, which lives in the build environment and not on the host that drives the board.

None of this proves the patches are correct, that the ELF was compiled from these bytes, or anything about a board. See the
design's list of non-proofs. The stock Zephyr project keeps its own refusal (clean and at the pin): nothing here accepts the
copy as that project.

Command line (the wrapper is scripts/freeze-patched-zephyr.sh)::

    patched_zephyr.py capture --copy DIR --out FILE [--marker-file NAME]
    patched_zephyr.py freeze  --copy DIR --zephyr DIR --patches DIR --manifest FILE --compile-commands FILE --pre FILE --out DIR
                              --inputs FILE --workspace DIR
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import posixpath
import re
import shutil
import subprocess
import sys
import tempfile

import zephyr_tree
from zephyr_tree import ALGORITHM, EXCLUDED

DEFAULT_MARKER_FILE = '.crux-wolf-tls'
# The commands of scripts/prepare-wolf-tls-zephyr.sh, so that the re-derivation does what the preparation did.
PATCH_COMMAND = ['patch', '--strip=1', '--no-backup-if-mismatch', '--quiet']
ARCHIVE_EXCLUDES = [':!doc', ':!samples', ':!tests']
RECORD_NAME = 'patched-zephyr.json'
LISTING_NAME = 'tree-files.tsv'
MARKER_NAME = 'marker'
OLD_KEYS = ('patch', 'patch_sha256', 'patched_file', 'patched_file_sha256')
BLOCK_KEYS = {'schema', 'decision', 'marker_file', 'pin', 'patches', 'patched_files', 'tree'}
RECORD_KEYS = {'schema', 'algorithm', 'excluded', 'pin', 'marker', 'tree', 'patches', 'patched_files', 'tree_listing',
               'compile_commands', 'manifest_block_sha256'}
_PIN = re.compile(r'[0-9a-f]{40}\Z')
_SHA = re.compile(r'[0-9a-f]{64}\Z')


def refuse(message):
    raise ValueError(message)


def sha256_bytes(data):
    return hashlib.sha256(data).hexdigest()


def canonical(value):
    """The canonical JSON of ``firmware_digest`` in scripts/record-build-provenance.py: sorted keys, compact separators."""
    return json.dumps(value, sort_keys=True, separators=(',', ':')).encode()


def block_sha256(block):
    return sha256_bytes(canonical(block))


def _is_int(value):
    return type(value) is int


def _malformed(check, *arguments):
    """Run a name or path check of the declaration and report its refusal as a malformed block."""
    try:
        check(*arguments)
    except ValueError as error:
        refuse('Malformed patched Zephyr block: %s' % error)


def check_marker_name(name):
    """The marker is one plain file name at the root of the copy."""
    if not isinstance(name, str) or not name or '/' in name or '\\' in name or name in ('.', '..', '.git') or \
            any(ord(c) < 0x20 or ord(c) == 0x7f for c in name):
        refuse('Marker file must be one plain file name: %r' % (name,))


def _excluded_path(path):
    parts = path.split('/')
    return (parts[-1] in EXCLUDED['names'] or parts[-1].endswith(EXCLUDED['suffixes']) or
            any(part in EXCLUDED['directories'] for part in parts[:-1]))


# ---- the declared block (schema 2) ---------------------------------------------------------------------------------

def validate_block(block):
    """The ``patched_zephyr`` block of the bench manifest, schema 2. Returns it, or raises ValueError.

    A missing block, a block without ``"schema": 2`` and a block with one of the old single-patch keys are refused as legacy,
    never read as a list of one."""
    if not isinstance(block, dict) or not _is_int(block.get('schema')) or block['schema'] != 2 or \
            any(key in block for key in OLD_KEYS):
        refuse('legacy or missing patched Zephyr block (schema 2 with lists of patches and patched files is required)')
    if set(block) != BLOCK_KEYS:
        refuse('Malformed patched Zephyr block: keys are %s, not %s' % (sorted(block), sorted(BLOCK_KEYS)))
    if not isinstance(block['decision'], str) or not block['decision']:
        refuse('Malformed patched Zephyr block: decision')
    _malformed(check_marker_name, block['marker_file'])
    if not isinstance(block['pin'], str) or _PIN.match(block['pin']) is None:
        refuse('Malformed patched Zephyr block: pin is not a full commit')
    patches = block['patches']
    if not isinstance(patches, list) or not patches:
        refuse('Malformed patched Zephyr block: patches is not a non-empty list')
    for patch in patches:
        if not isinstance(patch, dict) or set(patch) != {'name', 'sha256'}:
            refuse('Malformed patched Zephyr block: a patch has other keys than name and sha256')
        _malformed(zephyr_tree.check_patch_name, patch['name'])
        if not isinstance(patch['sha256'], str) or _SHA.match(patch['sha256']) is None:
            refuse('Malformed patched Zephyr block: patch hash of %s' % patch['name'])
    names = [p['name'] for p in patches]
    if len(set(names)) != len(names) or len({p['sha256'] for p in patches}) != len(patches):
        refuse('Malformed patched Zephyr block: duplicate patch name or hash')
    files = block['patched_files']
    if not isinstance(files, list) or not files:
        refuse('Malformed patched Zephyr block: patched_files is not a non-empty list')
    for item in files:
        if not isinstance(item, dict) or set(item) != {'path', 'change', 'sha256', 'patches'}:
            refuse('Malformed patched Zephyr block: a patched file has other keys than path, change, sha256 and patches')
        _malformed(zephyr_tree.check_patch_path, item['path'])
        if item['path'] == block['marker_file'] or _excluded_path(item['path']):
            refuse('Malformed patched Zephyr block: %s is the marker or an excluded name' % item['path'])
        if item['change'] not in ('added', 'modified', 'deleted'):
            refuse('Malformed patched Zephyr block: change of %s' % item['path'])
        deleted = item['change'] == 'deleted'
        if (item['sha256'] is None) != deleted or (not deleted and (not isinstance(item['sha256'], str) or _SHA.match(item['sha256']) is None)):
            refuse('Malformed patched Zephyr block: hash of %s (null exactly for a deleted file)' % item['path'])
        listed = item['patches']
        if not isinstance(listed, list) or not listed or any(n not in names for n in listed) or \
                listed != [n for n in names if n in listed]:
            refuse('Malformed patched Zephyr block: the patches of %s are not declared patches in order' % item['path'])
    paths = [item['path'] for item in files]
    if paths != sorted(set(paths), key=lambda p: p.encode('utf-8')):
        refuse('Malformed patched Zephyr block: patched_files are not unique and sorted by path bytes')
    tree = block['tree']
    if not isinstance(tree, dict) or set(tree) != {'algorithm', 'sha256', 'file_count'} or tree['algorithm'] != ALGORITHM or \
            not isinstance(tree['sha256'], str) or _SHA.match(tree['sha256']) is None or not _is_int(tree['file_count']) or \
            tree['file_count'] < 1:
        refuse('Malformed patched Zephyr block: tree must be %s with a 64-digit hash and a file count' % ALGORITHM)
    return block


# ---- capture -------------------------------------------------------------------------------------------------------

def _summary(digest):
    return dict(sha256=digest['sha256'], file_count=digest['file_count'], symlink_count=digest['symlink_count'])


def _read_marker(copy, marker_file):
    path = Path(copy) / marker_file
    if path.is_symlink() or not path.is_file():
        refuse('The marker %s is absent from the copy (or is not a regular file)' % marker_file)
    try:
        text = path.read_bytes().decode('utf-8')
    except UnicodeDecodeError:
        refuse('The marker %s is not UTF-8' % marker_file)
    try:
        zephyr_tree.parse_marker(text)
    except ValueError:
        refuse('The marker %s is malformed (expected "<pin> <patch hashes>" and one newline)' % marker_file)
    return text


def capture(copy, out, marker_file=DEFAULT_MARKER_FILE):
    """Step 2, before the build: the marker text and the tree digest of the copy. Refuses a copy that fails a rule of the digest."""
    check_marker_name(marker_file)
    copy = os.path.abspath(os.fspath(copy))
    digest = zephyr_tree.tree_digest(copy)
    text = _read_marker(copy, marker_file)
    record = dict(schema=1, algorithm=ALGORITHM, copy=copy, marker_file=marker_file, marker_text=text, tree=_summary(digest))
    with open(out, 'x') as handle:
        handle.write(json.dumps(record, indent=2, sort_keys=True) + '\n')
    return record


# ---- re-derivation -------------------------------------------------------------------------------------------------

def _need(program):
    if shutil.which(program) is None:
        refuse('The %s program was not found: the re-derivation of the tree cannot run (a skipped check is not a pass)' % program)


def _run(command, cwd=None, data=None):
    done = subprocess.run(command, cwd=cwd, input=data, capture_output=True)
    if done.returncode:
        refuse('%s failed (status %d): %s' % (command[0], done.returncode, (done.stderr or done.stdout).decode(errors='replace').strip()))
    return done.stdout


def rederive(zephyr, pin, patches, marker_file, scratch):
    """The stock tree from ``git archive <pin>`` of the clean checkout, and the same tree with the patches applied in order by the
    ``patch`` command line of the preparation script and then the EXPECTED marker written into it, each with its digest.

    ``patches`` is ``[(name, bytes)]`` in application order. ``scratch`` must not exist; it is created and left to the caller."""
    for program in ('patch', 'git', 'tar'):
        _need(program)
    check_marker_name(marker_file)
    scratch = Path(scratch)
    if scratch.exists():
        refuse('Scratch directory exists: %s' % scratch)
    stock, patched = scratch / 'stock', scratch / 'patched'
    stock.mkdir(parents=True)
    _run(['tar', '-x', '-C', str(stock)], data=_run(['git', '-C', str(zephyr), 'archive', pin] + ARCHIVE_EXCLUDES))
    shutil.copytree(stock, patched, symlinks=True)
    for name, data in patches:
        try:
            _run(PATCH_COMMAND, cwd=patched, data=data)
        except ValueError as error:
            refuse('Patch %s did not apply to the stock tree in this order: %s' % (name, error))
        for found in patched.rglob('*.rej'):
            refuse('Patch %s left a rejected hunk: %s' % (name, found.relative_to(patched)))
    text = zephyr_tree.marker_text(pin, [sha256_bytes(data) for _, data in patches])
    (patched / marker_file).write_text(text)
    stock_digest, patched_digest = zephyr_tree.tree_digest(stock), zephyr_tree.tree_digest(patched)
    return dict(stock=_summary(stock_digest), patched=_summary(patched_digest), stock_entries=stock_digest['entries'],
                patched_entries=patched_digest['entries'], marker_text=text)


# ---- compile commands ----------------------------------------------------------------------------------------------

def _under(path, base):
    return path == base or path.startswith(base.rstrip('/') + '/')


def compile_command_counts(database, copy, stock):
    """Which Zephyr tree the build compiled from: translation units under the copy and under the stock checkout, compared as
    normalised path strings (a different mount point is a refusal, not a translation). Raises ValueError on a malformed file."""
    try:
        entries = json.loads(Path(database).read_text())
    except (OSError, ValueError) as error:
        refuse('compile_commands.json is not readable JSON: %s' % error)
    if not isinstance(entries, list):
        refuse('compile_commands.json is not a list')
    copy, stock = posixpath.normpath(os.fspath(copy)), posixpath.normpath(os.fspath(stock))
    under_copy = under_stock = 0
    for entry in entries:
        if not isinstance(entry, dict) or not isinstance(entry.get('file'), str) or not entry['file'] or \
                not isinstance(entry.get('directory'), str):
            refuse('A compile_commands.json entry has no file and directory')
        path = posixpath.normpath(posixpath.join(entry['directory'], entry['file']))
        under_copy += _under(path, copy)
        under_stock += _under(path, stock)
    return dict(translation_units=len(entries), sources_under_copy=under_copy, sources_under_stock=under_stock)


# ---- freeze --------------------------------------------------------------------------------------------------------

def _overlap(a, b):
    return a == b or a.startswith(b.rstrip('/') + '/') or b.startswith(a.rstrip('/') + '/')


def _git(zephyr, *arguments):
    return _run(['git', '-C', str(zephyr)] + list(arguments)).decode(errors='replace').strip()


def _load_json(path, what):
    try:
        return json.loads(Path(path).read_text())
    except (OSError, ValueError) as error:
        refuse('%s is not readable JSON: %s' % (what, error))


def _west_projects(inputs, workspace, copy, zephyr):
    """The copy may not be, contain or sit inside a west project, and the stock checkout must be the west ``zephyr`` project."""
    data = _load_json(inputs, 'The inputs file')
    projects = data.get('west_projects') if isinstance(data, dict) else None
    if not isinstance(projects, list) or any(not isinstance(p, dict) or not isinstance(p.get('path'), str) for p in projects):
        refuse('The inputs file has no west_projects list of objects with a path')
    found = False
    for project in projects:
        where = posixpath.normpath(posixpath.join(os.fspath(workspace), project['path']))
        if _overlap(copy, where):
            refuse('The copy is, contains or sits inside the west project %s (%s)' % (project.get('name'), project['path']))
        if project.get('name') == 'zephyr':
            found = True
            if where != zephyr:
                refuse('The stock checkout is not the west project zephyr (%s)' % project['path'])
    if not found:
        refuse('The inputs file lists no west project zephyr: the stock checkout cannot be confirmed as that project')


def freeze(copy, zephyr, patches, manifest, compile_commands, pre, out, inputs, workspace):
    """Step 4, after the build. Checks everything in the design's list (a to f), then copies what it names into ``out`` and
    writes ``patched-zephyr.json`` there. Raises ValueError on any refusal, before ``out`` is created; returns the record."""
    copy, zephyr = os.path.abspath(os.fspath(copy)), os.path.abspath(os.fspath(zephyr))
    out, patches = Path(out), Path(patches)
    if out.exists():
        raise FileExistsError(out)
    if inputs is None or workspace is None:
        refuse('--inputs and --workspace are required: the copy is checked against the west projects')
    manifest_data = _load_json(manifest, 'The bench manifest')
    block = validate_block(manifest_data.get('patched_zephyr') if isinstance(manifest_data, dict) else None)
    marker_file = block['marker_file']
    captured = _load_json(pre, 'The pre-build capture')
    if not isinstance(captured, dict) or captured.get('schema') != 1 or captured.get('algorithm') != ALGORITHM or \
            captured.get('marker_file') != marker_file or not isinstance(captured.get('tree'), dict):
        refuse('The pre-build capture is not a schema 1 capture of this block')
    if captured.get('copy') != copy:
        refuse('The pre-build capture was made for another copy path: %r, not %r' % (captured.get('copy'), copy))
    if _overlap(copy, zephyr):
        refuse('The copy and the stock checkout overlap: the copy may not be, contain or sit inside the stock project')
    _west_projects(inputs, workspace, copy, zephyr)
    # The stock project keeps its own refusal: at the pin and clean, by the same checks as the build provenance.
    head = _git(zephyr, 'rev-parse', 'HEAD')
    if head != block['pin']:
        refuse('pin differs: the stock checkout is not at the pin of the block (%s, not %s)' % (head, block['pin']))
    dirty = _git(zephyr, 'status', '--porcelain=v1', '--untracked-files=all', '--', '.', ':(exclude)**/.DS_Store', ':(exclude).DS_Store')
    if dirty:
        refuse('The stock checkout is not clean: %s' % dirty.splitlines()[0])
    # (a) the tree digest again, equal to the pre-build one.
    post = zephyr_tree.tree_digest(copy)
    try:
        marker_text = _read_marker(copy, marker_file)
    except ValueError as error:
        refuse('marker differs: %s' % error)
    if _summary(post) != captured['tree'] or marker_text != captured.get('marker_text'):
        refuse('The copy changed since the capture before the build (tree digest or marker)')
    # (d) the patch bytes, by name.
    ordered = []
    for patch in block['patches']:
        path = patches / patch['name']
        if path.is_symlink() or not path.is_file():
            refuse('missing patch %s: not in the patch folder' % patch['name'])
        data = path.read_bytes()
        if sha256_bytes(data) != patch['sha256']:
            refuse('changed patch %s: its bytes do not hash to the declared value' % patch['name'])
        ordered.append((patch['name'], data))
    # the touched-file set parsed from each patch equals the declared patched files.
    net = zephyr_tree.net_changes([(name, zephyr_tree.patch_touched_files(data)) for name, data in ordered])
    declared = [dict(path=f['path'], change=f['change'], patches=f['patches']) for f in block['patched_files']]
    if net != declared:
        refuse('declared patched files differ from the files that the patches touch: %s' % sorted(
            {f['path'] for f in net} ^ {f['path'] for f in declared} or {f['path'] for f in net if f not in declared}))
    # (c) the marker equals the text built from the declared list.
    expected = zephyr_tree.marker_text(block['pin'], [p['sha256'] for p in block['patches']])
    if marker_text != expected:
        refuse('marker differs from "<pin> <hashes in declared order>"')
    # (e) the re-derivation, with the expected marker written before hashing; (b) the differing files, marker excluded by name.
    with tempfile.TemporaryDirectory() as scratch:
        derived = rederive(zephyr, block['pin'], ordered, marker_file, Path(scratch) / 'derive')
    copy_entries = {e['path']: e for e in post['entries']}
    stock_entries = {e['path']: e for e in derived['stock_entries']}
    differing = [p for p in zephyr_tree.differing_paths(post['entries'], derived['stock_entries']) if p != marker_file]
    declared_paths = [f['path'] for f in block['patched_files']]
    for path in differing:
        if path not in declared_paths:
            refuse('undeclared change in the copy against the stock tree: %s' % path)
    for path in declared_paths:
        if path not in differing:
            refuse('tree is not patched: the declared patched file %s equals the stock file' % path)
    patched_files = []
    for item in block['patched_files']:
        entry, stock_entry = copy_entries.get(item['path']), stock_entries.get(item['path'])
        final = entry['sha256'] if entry else None
        if (entry is not None and entry['kind'] != 'file') or final != item['sha256']:
            refuse('patched file differs: %s (final hash %s, declared %s)' % (item['path'], final, item['sha256']))
        patched_files.append(dict(item, stock_sha256=stock_entry['sha256'] if stock_entry else None,
                                  frozen_path=('files/' + item['path']) if entry else None,
                                  size_bytes=(Path(copy) / item['path']).stat().st_size if entry else None))
    if derived['patched']['sha256'] == derived['stock']['sha256']:
        refuse('tree is not patched: the re-derived patched tree equals the stock tree')
    if derived['patched']['sha256'] != post['sha256'] or derived['patched']['file_count'] != post['file_count']:
        refuse('tree digest differs: the tree re-derived from the stock checkout is not the copy')
    if post['sha256'] != block['tree']['sha256'] or post['file_count'] != block['tree']['file_count']:
        refuse('tree digest differs: the copy is not the tree that the declaration names')
    # (f) the compiled tree is the copy.
    counts = compile_command_counts(compile_commands, copy, zephyr)
    if counts['sources_under_stock'] != 0 or counts['sources_under_copy'] == 0:
        refuse('build was not from the copy: %d translation units under the stock checkout, %d under the copy' %
               (counts['sources_under_stock'], counts['sources_under_copy']))
    return _write(out, copy, block, ordered, patched_files, post, derived, counts, marker_text)


def _write(out, copy, block, ordered, patched_files, post, derived, counts, marker_text):
    try:
        out.mkdir()

        def put(relative, data):
            path = out / relative
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(data)
            return dict(path=relative, sha256=sha256_bytes(data), size_bytes=len(data))

        patches = []
        for name, data in ordered:
            patches.append(dict(name=name, **put('patches/' + name, data)))
        for item in patched_files:
            if item['frozen_path']:
                put(item['frozen_path'], (Path(copy) / item['path']).read_bytes())
        marker = dict(text=marker_text, **put(MARKER_NAME, marker_text.encode()))
        listing = ''.join('\t'.join((e['path'], e['kind'], e['flag'], e['sha256'])) + '\n' for e in post['entries'])
        tree_listing = put(LISTING_NAME, listing.encode())
        summary = _summary(post)
        record = dict(schema=1, algorithm=ALGORITHM, excluded={k: list(v) for k, v in EXCLUDED.items()}, pin=block['pin'], marker=marker,
                      tree=dict(pre=summary, post=summary, stock=derived['stock'], rederived=derived['patched']), patches=patches,
                      patched_files=patched_files, tree_listing=tree_listing, compile_commands=counts,
                      manifest_block_sha256=block_sha256(block))
        (out / RECORD_NAME).write_text(json.dumps(record, indent=2, sort_keys=True) + '\n')
        return record
    except BaseException:
        shutil.rmtree(out, ignore_errors=True)
        raise


def named_files(record_path):
    """Every file that the record names, as ``{relative path: {sha256, size_bytes}}`` relative to the folder of the record.
    The record itself is not in the list. Raises ValueError on a malformed record."""
    record = _load_json(record_path, 'The patched Zephyr record')
    try:
        return _named(record)
    except (KeyError, TypeError) as error:
        refuse('record and manifest differ: malformed record (%r)' % (error,))


# ---- the check that check_image runs -------------------------------------------------------------------------------

def _frozen_bytes(folder, relative, what):
    path = Path(folder) / relative
    try:
        safe = isinstance(relative, str) and relative and not relative.startswith('/') and '\\' not in relative and \
            all(part not in ('', '.', '..') for part in relative.split('/'))
        if not safe or path.is_symlink() or not path.is_file():
            refuse('frozen artefact changed: %s is missing or unsafe (%s)' % (what, relative))
        return path.read_bytes()
    except OSError as error:
        refuse('frozen artefact changed: %s is unreadable (%s)' % (what, error))


def check_record(manifest_path, record_path, expected_pin):
    """Verify the frozen patched Zephyr evidence against the frozen manifest block. Reads frozen bytes only; never the copy.
    Returns the facts for the image record, or raises ValueError with one of the refusals of the design's table."""
    manifest = _load_json(manifest_path, 'The frozen bench manifest')
    block = validate_block(manifest.get('patched_zephyr') if isinstance(manifest, dict) else None)
    try:
        record = json.loads(Path(record_path).read_text())
    except (OSError, ValueError) as error:
        refuse('record and manifest differ: the record is missing or is not JSON (%s)' % error)
    folder = Path(record_path).parent
    try:
        return _check(block, record, folder, expected_pin)
    except (KeyError, TypeError, AttributeError, IndexError) as error:
        refuse('record and manifest differ: malformed record (%r)' % (error,))


def _check(block, record, folder, expected_pin):
    if not isinstance(record, dict) or set(record) != RECORD_KEYS or not _is_int(record['schema']) or record['schema'] != 1 or \
            record['algorithm'] != ALGORITHM or block['tree']['algorithm'] != ALGORITHM:
        refuse('record and manifest differ: the record is missing, has another shape, schema or algorithm')
    if record['excluded'] != {key: list(value) for key, value in EXCLUDED.items()}:
        refuse('record and manifest differ: the record excluded other names than the algorithm does')
    if record['manifest_block_sha256'] != block_sha256(block):
        refuse('record and manifest differ: the record was made for another manifest block')
    declared = {p['name']: p['sha256'] for p in block['patches']}
    names = [p['name'] for p in record['patches']]
    for name in declared:
        if name not in names:
            refuse('missing patch %s: not in the record' % name)
    for name in names:
        if name not in declared:
            refuse('unexpected patch %s: the block does not declare it' % name)
    if names != list(declared) or len(set(names)) != len(names):
        refuse('patches out of order: the record lists %s, the block %s' % (names, list(declared)))
    for patch in record['patches']:
        name = patch['name']
        if patch['sha256'] != declared[name]:
            refuse('changed patch %s: the record names another hash than the block' % name)
        path = Path(folder) / ('patches/' + name)
        if patch['path'] != 'patches/' + name:
            refuse('frozen artefact changed: unexpected path for patch %s' % name)
        if path.is_symlink() or not path.is_file():
            refuse('missing patch %s: its frozen artefact is absent' % name)
        if sha256_bytes(path.read_bytes()) != declared[name]:
            refuse('changed patch %s: its frozen bytes do not hash to the block value' % name)
    # the marker: well-formed, the declared pin and the declared hashes in the declared order.
    expected = zephyr_tree.marker_text(block['pin'], list(declared.values()))
    marker = record['marker']
    try:
        text = _frozen_bytes(folder, marker['path'], 'the marker').decode('utf-8')
    except (UnicodeDecodeError, ValueError) as error:
        if 'marker differs' in str(error) or isinstance(error, UnicodeDecodeError):
            refuse('marker differs: the frozen marker is not UTF-8')
        refuse('marker differs: the frozen marker is absent')
    if marker['path'] != MARKER_NAME or text != marker['text']:
        refuse('marker differs: the record and the frozen marker disagree')
    try:
        marker_pin, marker_hashes = zephyr_tree.parse_marker(text)
    except ValueError:
        refuse('marker differs: it is not "<pin> <hashes>" and one newline')
    by_hash = {h: n for n, h in declared.items()}
    for name, digest in declared.items():
        if digest not in marker_hashes:
            refuse('missing patch %s: not in the marker' % name)
    for digest in marker_hashes:
        if digest not in by_hash:
            refuse('unexpected patch %s: the marker lists a hash that the block does not declare' % digest[:12])
    if marker_hashes != list(declared.values()):
        refuse('patches out of order: the marker lists the hashes in another order than the block')
    if text != expected:
        refuse('marker differs: it is not "<pin> <hashes in block order>"')
    if record['pin'] != block['pin'] or block['pin'] != expected_pin or marker_pin != block['pin']:
        refuse('pin differs: the block, the record and the profile must name one Zephyr revision')
    # the patched files.
    tree = record['tree']
    if tree['stock']['sha256'] == tree['post']['sha256']:
        refuse('tree is not patched: the re-derived stock digest equals the copy digest')
    in_record = {f['path']: f for f in record['patched_files']}
    if len(in_record) != len(record['patched_files']):
        refuse('patched file differs: a path is listed twice in the record')
    for item in block['patched_files']:
        if item['path'] not in in_record:
            refuse('tree is not patched: the patched file %s is missing from the record' % item['path'])
    for item in record['patched_files']:
        stock = item['stock_sha256']
        if (stock is None) != (item['change'] == 'added') or (stock is not None and (not isinstance(stock, str) or _SHA.match(stock) is None)):
            refuse('patched file differs: %s has a stock hash that does not fit its change (null exactly for an added file)' % item['path'])
        if item['change'] == 'modified' and item['sha256'] is not None and item['sha256'] == stock:
            refuse('tree is not patched: %s equals its stock hash' % item['path'])
    wanted = {f['path']: f for f in block['patched_files']}
    for path in in_record:
        if path not in wanted:
            refuse('patched file differs: %s is in the record and not in the block' % path)
    for path, item in wanted.items():
        mine = in_record[path]
        if (mine['change'], mine['sha256'], mine['patches']) != (item['change'], item['sha256'], item['patches']):
            refuse('patched file differs: %s (change, final hash or patches differ from the block)' % path)
    # the tree digest.
    expected_tree = block['tree']
    if tree['pre'] != tree['post'] or tree['post']['sha256'] != expected_tree['sha256'] or \
            tree['post']['file_count'] != expected_tree['file_count'] or tree['rederived'] != tree['post']:
        refuse('tree digest differs: the record, the block, the pre and post captures and the re-derivation must agree')
    # the compiled tree is the copy.
    counts = record['compile_commands']
    if not _is_int(counts['sources_under_stock']) or not _is_int(counts['sources_under_copy']) or \
            counts['sources_under_stock'] != 0 or counts['sources_under_copy'] < 1:
        refuse('build was not from the copy: the compile commands name the stock checkout or no source of the copy')
    # every frozen artefact the record names is present and hashes to the record's value.
    expected_paths = {('patches/' + n) for n in names} | {MARKER_NAME, LISTING_NAME} | \
        {'files/' + f['path'] for f in record['patched_files'] if f['change'] != 'deleted'}
    for item in record['patched_files']:
        if (item['frozen_path'] is None) != (item['change'] == 'deleted') or \
                (item['frozen_path'] is not None and item['frozen_path'] != 'files/' + item['path']):
            refuse('frozen artefact changed: the frozen path of %s is not the expected one' % item['path'])
    if record['tree_listing']['path'] != LISTING_NAME:
        refuse('frozen artefact changed: the tree listing is not at its expected path')
    named = _named(record)
    for relative, spec in named.items():
        data = _frozen_bytes(folder, relative, 'a file that the record names')
        if len(data) != spec['size_bytes'] or sha256_bytes(data) != spec['sha256']:
            refuse('frozen artefact changed: %s does not hash to the value in the record' % relative)
    if set(named) != expected_paths:
        refuse('frozen artefact changed: the record names other files than the expected ones')
    return dict(pin=block['pin'], decision=block['decision'], marker_file=block['marker_file'],
                patches=[dict(p) for p in block['patches']],
                patched_files=[dict(path=f['path'], change=f['change'], sha256=f['sha256'], patches=list(f['patches']))
                               for f in block['patched_files']],
                tree=dict(block['tree']), manifest_block_sha256=record['manifest_block_sha256'])


def _named(record):
    named = {}
    for patch in record['patches']:
        named[patch['path']] = dict(sha256=patch['sha256'], size_bytes=patch['size_bytes'])
    for item in record['patched_files']:
        if item['frozen_path'] is not None:
            named[item['frozen_path']] = dict(sha256=item['sha256'], size_bytes=item['size_bytes'])
    for key in ('marker', 'tree_listing'):
        named[record[key]['path']] = dict(sha256=record[key]['sha256'], size_bytes=record[key]['size_bytes'])
    return named


# ---- command line ----------------------------------------------------------------------------------------------------

def build_parser():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    actions = parser.add_subparsers(dest='action', required=True)
    first = actions.add_parser('capture', help='before the build: marker text and tree digest of the copy')
    first.add_argument('--copy', type=Path, required=True)
    first.add_argument('--out', type=Path, required=True)
    first.add_argument('--marker-file', default=DEFAULT_MARKER_FILE)
    second = actions.add_parser('freeze', help='after the build: check everything, then freeze the evidence')
    for name in ('copy', 'zephyr', 'patches', 'manifest', 'compile-commands', 'pre', 'out'):
        second.add_argument('--' + name, type=Path, required=True)
    second.add_argument('--inputs', type=Path, required=True, help='the inputs file of record-build-provenance.py --capture-inputs (step 1)')
    second.add_argument('--workspace', type=Path, required=True, help='the west workspace that the inputs file names its projects in')
    return parser


def main(argv=None):
    args = build_parser().parse_args(argv)
    try:
        if args.action == 'capture':
            record = capture(args.copy, args.out, args.marker_file)
            print(json.dumps(dict(tree_sha256=record['tree']['sha256'], file_count=record['tree']['file_count'])))
        else:
            record = freeze(args.copy, args.zephyr, args.patches, args.manifest, args.compile_commands, args.pre, args.out,
                            inputs=args.inputs, workspace=args.workspace)
            print(json.dumps(dict(tree_sha256=record['tree']['post']['sha256'], patches=[p['name'] for p in record['patches']],
                                  record_sha256=sha256_bytes((args.out / RECORD_NAME).read_bytes()))))
    except (ValueError, FileExistsError) as error:
        print('FAIL: %s' % error, file=sys.stderr)
        return 1
    return 0


if __name__ == '__main__':
    sys.exit(main())
