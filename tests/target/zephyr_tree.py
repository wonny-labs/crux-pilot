"""The tree digest ``crux-zephyr-tree-v1``, the marker text and the patch parser of the bench freeze (design B2).

Design: docs/rt1060-bench-freeze-and-capture-design.md, parts 3 and 2 and the patch rules of part 1. Pure functions of file
bytes: no network, no board, no git. ``patched_zephyr.py`` builds the freeze and the image check on these.

The digest identifies the bytes of a copy of the pinned Zephyr tree. It does not say the bytes are correct, and the files
it ignores (empty directories, the excluded names, every mode bit but the owner's executable bit, owners, times) are not
part of what it proves.
"""
import hashlib
import os
import posixpath
import re
import stat

ALGORITHM = 'crux-zephyr-tree-v1'
# The exact exclusions of the algorithm. A change of this list is a new algorithm, not a new value of this one.
EXCLUDED = dict(names=('.DS_Store',), directories=('__pycache__',), suffixes=('.pyc', '.pyo'))
_CHUNK = 1 << 20


def _refuse(message):
    raise ValueError(message)


def relative_name(raw):
    """A relative POSIX name from its raw bytes: valid UTF-8, no control character. Raises ValueError otherwise."""
    try:
        name = raw.decode('utf-8')
    except UnicodeDecodeError:
        _refuse('Name is not valid UTF-8: %r' % raw)
    if any(ord(c) < 0x20 or ord(c) == 0x7f for c in name):
        _refuse('Name has a control character: %r' % name)
    return name


def check_names(names):
    """Refuse two names, or a name and a directory component, that are equal under case folding but spelled differently."""
    seen = {}
    for name in names:
        parts = name.split('/')
        for end in range(1, len(parts) + 1):
            prefix = '/'.join(parts[:end])
            if seen.setdefault(prefix.casefold(), prefix) != prefix:
                _refuse('Names equal under case folding: %r and %r' % (seen[prefix.casefold()], prefix))


def _excluded(name, directory):
    if directory:
        return name in EXCLUDED['directories']
    return name in EXCLUDED['names'] or name.endswith(EXCLUDED['suffixes'])


def _symlink_entry(relative, target_raw, path, root_real):
    target = relative_name(target_raw)
    inside = posixpath.normpath(posixpath.join(posixpath.dirname(relative), target)) if target else '..'
    if not target or target.startswith('/') or inside == '..' or inside.startswith('../') or inside.startswith('/'):
        _refuse('Symlink %s leaves the tree or is absolute: %s' % (relative, target))
    # The text can look harmless and still lead out through another link (a/s -> .., a/x -> s/../..): resolve it for real.
    real = os.path.realpath(path)
    if os.path.commonpath([real, root_real]) != root_real:
        _refuse('Symlink %s resolves outside the tree: %s' % (relative, real))
    return hashlib.sha256(target_raw).hexdigest()


def _file_hash(path):
    digest = hashlib.sha256()
    try:
        with open(path, 'rb') as handle:
            for chunk in iter(lambda: handle.read(_CHUNK), b''):
                digest.update(chunk)
    except OSError as error:
        _refuse('Cannot read %r: %s' % (path, error))
    return digest.hexdigest()


def _walk(directory, prefix, entries, root_real):
    try:
        listing = list(os.scandir(directory))
    except OSError as error:
        _refuse('Cannot list %r: %s' % (directory, error))
    for item in listing:
        raw = os.fsencode(item.name)
        relative = prefix + relative_name(raw)
        if raw.lower() == b'.git':
            _refuse('A .git entry in the tree: %s' % relative)
        try:
            link = item.is_symlink()
            directory_entry = not link and item.is_dir(follow_symlinks=False)
            status = None if (link or directory_entry) else item.stat(follow_symlinks=False)
        except OSError as error:
            _refuse('Cannot inspect %r: %s' % (relative, error))
        if _excluded(item.name, directory_entry):
            continue
        if directory_entry:
            _walk(item.path, relative + '/', entries, root_real)
        elif link:
            entries.append(dict(path=relative, kind='symlink', flag='plain',
                                sha256=_symlink_entry(relative, os.readlink(os.fsencode(item.path)), item.path, root_real)))
        elif stat.S_ISREG(status.st_mode):
            flag = 'exec' if status.st_mode & stat.S_IXUSR else 'plain'
            entries.append(dict(path=relative, kind='file', flag=flag, sha256=_file_hash(item.path)))
        else:
            _refuse('Entry is not a regular file, a symlink or a directory: %s' % relative)


def tree_digest(root):
    """The digest of the tree under ``root`` and its entries, sorted by the bytes of their names. Raises ValueError on any refusal."""
    root = os.fspath(root)
    try:
        status = os.lstat(root)
    except OSError as error:
        _refuse('Tree root is not readable: %s' % error)
    if not stat.S_ISDIR(status.st_mode):
        _refuse('Tree root is not a directory (a symlink is refused too): %s' % root)
    entries = []
    _walk(root, '', entries, os.path.realpath(root))
    entries.sort(key=lambda entry: entry['path'].encode('utf-8'))
    check_names([entry['path'] for entry in entries])
    digest = hashlib.sha256()
    for entry in entries:
        digest.update(entry['path'].encode('utf-8') + b'\0' + entry['kind'].encode() + b'\0' + entry['flag'].encode() + b'\0' +
                      bytes.fromhex(entry['sha256']))
    return dict(algorithm=ALGORITHM, sha256=digest.hexdigest(),
                file_count=sum(entry['kind'] == 'file' for entry in entries),
                symlink_count=sum(entry['kind'] == 'symlink' for entry in entries), entries=entries)


def differing_paths(left, right):
    """The paths whose entry (kind, flag, content hash) differs between two lists of entries, or that only one list has."""
    a = {e['path']: (e['kind'], e['flag'], e['sha256']) for e in left}
    b = {e['path']: (e['kind'], e['flag'], e['sha256']) for e in right}
    return sorted((p for p in set(a) | set(b) if a.get(p) != b.get(p)), key=lambda p: p.encode('utf-8'))


# ---- the marker ----------------------------------------------------------------------------------------------------

_PIN = re.compile(r'[0-9a-f]{40}\Z')
_SHA = re.compile(r'[0-9a-f]{64}\Z')
_MARKER = re.compile(r'([0-9a-f]{40})((?: [0-9a-f]{64})+)\n\Z')


def marker_text(pin, patch_sha256s):
    """``<pin> <sha256 of patch 1> <sha256 of patch 2> ...`` and a newline. For one patch it is what
    scripts/prepare-wolf-tls-zephyr.sh writes: ``printf '%s %s\\n' "$zephyr_pin" "$patch_sha256"``."""
    hashes = list(patch_sha256s)
    if _PIN.match(str(pin)) is None or not hashes or any(_SHA.match(str(h)) is None for h in hashes) or len(set(hashes)) != len(hashes):
        _refuse('A marker needs a 40-digit pin and one or more distinct 64-digit patch hashes')
    return '%s %s\n' % (pin, ' '.join(hashes))


def parse_marker(text):
    """The pin and the ordered hashes of a marker text, which must be exactly the form of ``marker_text``."""
    match = _MARKER.match(text) if isinstance(text, str) else None
    if match is None:
        _refuse('Malformed marker text')
    return match.group(1), match.group(2).split()


# ---- the patch parser ----------------------------------------------------------------------------------------------

_PATCH_NAME = re.compile(r'[A-Za-z0-9][A-Za-z0-9._+-]*\Z')
_HEADER = re.compile(r'diff --git a/(\S+) b/(\S+)\Z')
_CHANGES = ('added', 'modified', 'deleted')


def check_patch_name(name):
    """A patch name is a plain file name: ASCII letters, digits and ``._+-``, not starting with a dot or a dash."""
    if not isinstance(name, str) or _PATCH_NAME.match(name) is None:
        _refuse('Patch name is not a plain file name: %r' % (name,))


def check_patch_path(path):
    """A path that a patch touches: relative, POSIX, no empty, ``.`` or ``..`` part, no quote, no control character, no .git."""
    parts = path.split('/')
    if (not path or '\\' in path or '"' in path or any(p in ('', '.', '..') or p.lower() == '.git' for p in parts) or
            any(ord(c) < 0x20 or ord(c) == 0x7f for c in path)):
        _refuse('Unsafe path in a patch: %r' % path)


def _section_change(path, lines):
    header = []
    for line in lines:
        if line.startswith(b'@@'):
            break
        header.append(line)
    hunk = any(line.startswith(b'@@') for line in lines)
    names = {}
    for line in header:
        for key in (b'new file mode ', b'deleted file mode ', b'old mode ', b'new mode ', b'similarity index ',
                    b'dissimilarity index ', b'rename from ', b'rename to ', b'copy from ', b'copy to '):
            if line.startswith(key):
                names.setdefault(key, []).append(line)
    if any(key in names for key in (b'old mode ', b'new mode ')):
        _refuse('Patch changes a file mode (not understood): %s' % path)
    if any(key in names for key in (b'similarity index ', b'dissimilarity index ', b'rename from ', b'rename to ', b'copy from ', b'copy to ')):
        _refuse('Patch renames or copies a file (not understood): %s' % path)
    if any(line == b'GIT binary patch' or line.startswith(b'Binary files ') for line in lines):
        _refuse('Patch has a binary hunk (not understood): %s' % path)
    old = [line for line in header if line.startswith(b'--- ')]
    new = [line for line in header if line.startswith(b'+++ ')]
    added, deleted = b'new file mode ' in names, b'deleted file mode ' in names
    if added and deleted:
        _refuse('Patch both adds and deletes a file: %s' % path)
    if len(old) > 1 or len(new) > 1 or bool(old) != bool(new):
        _refuse('Malformed ---/+++ lines for %s' % path)
    old_name = old[0][4:].split(b'\t')[0].decode('utf-8', 'replace') if old else None
    new_name = new[0][4:].split(b'\t')[0].decode('utf-8', 'replace') if new else None
    if added:
        if old and (old_name != '/dev/null' or new_name != 'b/' + path):
            _refuse('Added file %s has inconsistent ---/+++ lines' % path)
        return 'added'
    if deleted:
        if old and (old_name != 'a/' + path or new_name != '/dev/null'):
            _refuse('Deleted file %s has inconsistent ---/+++ lines' % path)
        return 'deleted'
    if not old or not hunk or old_name != 'a/' + path or new_name != 'b/' + path:
        _refuse('Modified file %s has no matching ---/+++ lines and hunk (or is a /dev/null side without a new or deleted file header)' % path)
    return 'modified'


_HUNK = re.compile(rb'@@ -\d+(?:,(\d+))? \+\d+(?:,(\d+))? @@')


def patch_touched_files(data):
    """``[(path, change)]`` in patch order from a git-style unified diff, ``change`` being added, modified or deleted.

    Only text additions, modifications and deletions are understood. A rename, a copy, a mode change and a binary hunk are
    refused, and so is a patch that touches nothing. Text before the first section (a mail header, a commit message) and
    after the last hunk (a mail trailer) is ignored. Every line that begins ``diff --git `` must be a well-formed
    ``diff --git a/x b/x`` header, wherever it is, and a plain unified-diff section (``--- ``, ``+++ `` lines without a git header,
    before the first section or after a hunk) is refused: a section that this parser skipped would be a patched file that the
    declared list does not name. Hunks are read with their line counts, so a content line that looks like a header is content."""
    sections = []
    old = new = 0                                        # lines still owed by the open hunk
    lines = data.split(b'\n')
    for index, raw in enumerate(lines):
        if old or new:
            if raw[:1] in (b' ', b'') :
                old, new = old - 1, new - 1
            elif raw[:1] == b'-':
                old -= 1
            elif raw[:1] == b'+':
                new -= 1
            elif raw[:1] != b'\\':
                _refuse('A hunk of %s is shorter than its header says' % sections[-1][0])
            if old < 0 or new < 0:
                _refuse('A hunk of %s is longer than its header says' % sections[-1][0])
            sections[-1][1].append(raw)
            continue
        if raw.startswith(b'diff --git '):
            try:
                text = raw.decode('utf-8')
            except UnicodeDecodeError:
                _refuse('A diff --git line is not UTF-8')
            match = _HEADER.match(text)
            if match is None or match.group(1) != match.group(2):
                _refuse('Malformed or renaming diff --git header (quoted names and names with spaces are not understood): %r' % text)
            sections.append((match.group(1), [], False))
            continue
        if raw.startswith(b'--- ') and index + 1 < len(lines) and lines[index + 1].startswith(b'+++ ') and \
                (not sections or sections[-1][2]):
            _refuse('A plain unified-diff section (no diff --git header) is not understood: %r' % raw[:80])
        if not sections:
            continue
        if raw.startswith(b'@@'):
            match = _HUNK.match(raw)
            if match is None:
                _refuse('Malformed hunk header in %s: %r' % (sections[-1][0], raw[:80]))
            old = int(match.group(1) if match.group(1) is not None else 1)
            new = int(match.group(2) if match.group(2) is not None else 1)
            sections[-1] = (sections[-1][0], sections[-1][1], True)
        sections[-1][1].append(raw)
    if old or new:
        _refuse('The last hunk is shorter than its header says')
    touched = []
    seen = set()
    for path, section, _ in sections:
        check_patch_path(path)
        if path in seen:
            _refuse('A patch touches %s twice' % path)
        seen.add(path)
        touched.append((path, _section_change(path, section)))
    if not touched:
        _refuse('The patch touches no file that this parser understands (no git-style diff --git header)')
    return touched


def net_changes(patches):
    """The net effect of ordered patches. ``patches`` is ``[(name, [(path, change)])]``. Returns, sorted by path bytes,
    ``[dict(path, change, patches)]`` where ``patches`` lists the names of the patches that touch the file in order and
    ``change`` is relative to the stock tree. A sequence that no ``patch`` run could apply is refused."""
    names = [name for name, _ in patches]
    if len(set(names)) != len(names):
        _refuse('Duplicate patch name')
    history = {}
    for name, touched in patches:
        for path, change in touched:
            if change not in _CHANGES:
                _refuse('Unknown change %r' % (change,))
            history.setdefault(path, []).append((name, change))
    result = []
    for path in sorted(history, key=lambda p: p.encode('utf-8')):
        steps = history[path]
        stock = steps[0][1] != 'added'
        exists = stock
        for name, change in steps:
            if (change == 'added') == exists or (change != 'added' and not exists):
                _refuse('Patch %s cannot %s %s here' % (name, change, path))
            exists = change != 'deleted'
        if not stock and not exists:
            _refuse('%s is added and deleted again: no net change' % path)
        result.append(dict(path=path, change='modified' if stock and exists else 'added' if exists else 'deleted',
                           patches=[name for name, _ in steps]))
    return result
