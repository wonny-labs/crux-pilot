"""The tree digest ``crux-zephyr-tree-v1``, the marker text and the patch parser of the bench freeze (B2, tests T1 to T5).

Design: docs/rt1060-bench-freeze-and-capture-design.md. Host only: no Docker, no board, no network. The fixtures are small
synthetic trees built inside the test. Two cases need a file system that can hold what the digest refuses (names that
differ only in case, a name that is not UTF-8); where the file system cannot, the test is skipped and reported as skipped,
and the same rule is also tested on the name list alone, which always runs.
"""
import hashlib
import os
from pathlib import Path
import stat
import subprocess
import sys
import tempfile
import unittest

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]
sys.path.insert(0, str(HERE.parent / 'target'))
import zephyr_tree as subject

KNOWN_ANSWER = 'be2b439b49e056b45fc1936b13af0ed0c679b1841d773e634680d90144e70671'
PIN = 'dccb09599635bdff17633fa7e9dab014b91dce90'
PATCH_ONE = '3d1cc0307bbeea5f000698bca5be717f73fdb8c47e90083b003336364e8fdbda'
PATCH_TWO = 'a' * 64


def reference_digest(root):
    """A second implementation of the rule, written differently: it walks bottom-up, keeps a dict and sorts once at the end."""
    root = str(root)
    found = {}
    for here, directories, files in os.walk(root, topdown=False, followlinks=False):
        for name in files + [d for d in directories if os.path.islink(os.path.join(here, d))]:
            if name == '.DS_Store' or name.endswith(('.pyc', '.pyo')):
                continue
            full = os.path.join(here, name)
            relative = os.path.relpath(full, root).replace(os.sep, '/')
            if '__pycache__' in relative.split('/'):
                continue
            if os.path.islink(full):
                kind, flag, content = 'symlink', 'plain', os.readlink(full).encode()
            else:
                kind = 'file'
                flag = 'exec' if os.stat(full).st_mode & stat.S_IXUSR else 'plain'
                content = Path(full).read_bytes()
            found[relative.encode()] = (kind, flag, hashlib.sha256(content).digest())
    digest = hashlib.sha256()
    for path in sorted(found):
        kind, flag, content = found[path]
        digest.update(path + b'\0' + kind.encode() + b'\0' + flag.encode() + b'\0' + content)
    return digest.hexdigest(), len(found)


def put(root, relative, data=b'x\n', mode=0o644):
    path = Path(root) / relative
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(data)
    os.chmod(path, mode)


def tiny_tree(root):
    """Files, an executable file, two symlinks, an empty directory, a .DS_Store and a __pycache__: the known-answer tree."""
    root = Path(root)
    (root / 'dir' / '__pycache__').mkdir(parents=True)
    (root / 'empty').mkdir()
    for relative, data, mode in (('a.txt', b'alpha\n', 0o644), ('dir/b.bin', b'\x00\x01', 0o600),
                                 ('dir/run.sh', b'#!/bin/sh\n', 0o755), ('.DS_Store', b'junk', 0o644),
                                 ('dir/__pycache__/x.pyc', b'\x00pyc', 0o644)):
        put(root, relative, data, mode)
    os.symlink('a.txt', root / 'link')
    os.symlink('b.bin', root / 'dir' / 'rel-link')


class Base(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name) / 'tree'
        self.root.mkdir()

    def digest(self, root=None):
        return subject.tree_digest(root or self.root)['sha256']


class KnownAnswer(Base):
    """T1."""

    def test_the_digest_of_the_tiny_tree_is_a_literal(self):
        tiny_tree(self.root)
        result = subject.tree_digest(self.root)
        self.assertEqual(result['sha256'], KNOWN_ANSWER)
        self.assertEqual((result['algorithm'], result['file_count'], result['symlink_count']), ('crux-zephyr-tree-v1', 3, 2))

    def test_a_second_implementation_with_another_walk_order_agrees(self):
        tiny_tree(self.root)
        self.assertEqual(reference_digest(self.root), (KNOWN_ANSWER, 5))
        put(self.root, 'zz/deep/file.c', b'int x;\n')
        self.assertEqual(reference_digest(self.root)[0], self.digest())

    def test_the_entries_are_listed_in_byte_order_with_their_framing_fields(self):
        tiny_tree(self.root)
        entries = subject.tree_digest(self.root)['entries']
        self.assertEqual([(e['path'], e['kind'], e['flag']) for e in entries],
                         [('a.txt', 'file', 'plain'), ('dir/b.bin', 'file', 'plain'), ('dir/rel-link', 'symlink', 'plain'),
                          ('dir/run.sh', 'file', 'exec'), ('link', 'symlink', 'plain')])
        self.assertEqual(entries[0]['sha256'], hashlib.sha256(b'alpha\n').hexdigest())
        self.assertEqual(entries[4]['sha256'], hashlib.sha256(b'a.txt').hexdigest())     # the link's target text, never its content

    def test_the_exclusion_list_is_exactly_the_reviewed_one(self):
        self.assertEqual(subject.ALGORITHM, 'crux-zephyr-tree-v1')
        self.assertEqual(subject.EXCLUDED, dict(names=('.DS_Store',), directories=('__pycache__',), suffixes=('.pyc', '.pyo')))

    def test_sorting_is_by_bytes_not_by_locale(self):
        for name in ('b', 'A', 'a2', 'é', 'Z'):
            put(self.root, name)
        paths = [e['path'] for e in subject.tree_digest(self.root)['entries']]
        self.assertEqual(paths, sorted(paths, key=lambda p: p.encode()))
        self.assertEqual(paths, ['A', 'Z', 'a2', 'b', 'é'])


class Determinism(Base):
    """T2."""

    def test_creation_order_mtimes_modes_and_owner_do_not_change_the_digest(self):
        tiny_tree(self.root)
        other = Path(self.temp.name) / 'other'
        other.mkdir()
        # the same tree, created in the opposite order, with other mtimes and other non-executable mode bits
        os.symlink('b.bin', other / 'dir-rel-link-placeholder')
        os.unlink(other / 'dir-rel-link-placeholder')
        (other / 'empty').mkdir()
        os.symlink('a.txt', other / 'link')
        put(other, 'dir/run.sh', b'#!/bin/sh\n', 0o700)                    # still executable
        put(other, 'dir/b.bin', b'\x00\x01', 0o664)                        # still not
        put(other, 'a.txt', b'alpha\n', 0o640)
        os.symlink('b.bin', other / 'dir' / 'rel-link')
        os.utime(other / 'a.txt', (1, 1))
        os.utime(other / 'dir', (5, 5))
        self.assertEqual(self.digest(), self.digest(other))
        self.assertEqual(self.digest(other), KNOWN_ANSWER)

    def test_only_the_executable_bit_changes_the_digest(self):
        tiny_tree(self.root)
        os.chmod(self.root / 'dir' / 'run.sh', 0o644)
        self.assertNotEqual(self.digest(), KNOWN_ANSWER)
        os.chmod(self.root / 'dir' / 'run.sh', 0o755)
        self.assertEqual(self.digest(), KNOWN_ANSWER)
        os.chmod(self.root / 'a.txt', 0o755)
        self.assertNotEqual(self.digest(), KNOWN_ANSWER)

    def test_the_digest_does_not_depend_on_the_root_path_or_name(self):
        tiny_tree(self.root)
        elsewhere = Path(self.temp.name) / 'a' / 'b' / 'another-name'
        elsewhere.parent.mkdir(parents=True)
        tiny_tree(elsewhere.parent / 'x')
        (elsewhere.parent / 'x').rename(elsewhere)
        self.assertEqual(self.digest(), self.digest(elsewhere))

    def test_the_digest_may_be_taken_twice_with_the_same_result(self):
        tiny_tree(self.root)
        self.assertEqual(subject.tree_digest(self.root), subject.tree_digest(self.root))


class SensitivityAndRefusals(Base):
    """T3."""

    def setUp(self):
        super().setUp()
        tiny_tree(self.root)

    def test_a_difference_of_any_kind_changes_the_digest(self):
        base = self.digest()
        cases = {
            'one byte': lambda: put(self.root, 'a.txt', b'alphb\n'),
            'crlf against lf': lambda: put(self.root, 'a.txt', b'alpha\r\n'),
            'a rename': lambda: os.rename(self.root / 'a.txt', self.root / 'a2.txt'),
            'a symlink target': lambda: (os.unlink(self.root / 'link'), os.symlink('dir/b.bin', self.root / 'link')),
            'an extra file': lambda: put(self.root, 'new.c'),
            'a missing file': lambda: os.unlink(self.root / 'dir' / 'b.bin'),
            'a file in place of a link': lambda: (os.unlink(self.root / 'link'), put(self.root, 'link', b'a.txt')),
        }
        for label, change in cases.items():
            with self.subTest(label):
                self.setUp()
                change()
                self.assertNotEqual(self.digest(), base)

    def test_a_file_in_place_of_a_link_with_the_target_text_as_content_is_still_another_entry_kind(self):
        # the framing carries the kind, so a file whose content equals a link's target text is not that link
        os.unlink(self.root / 'link')
        put(self.root, 'link', b'a.txt')
        entries = {e['path']: e for e in subject.tree_digest(self.root)['entries']}
        self.assertEqual(entries['link']['kind'], 'file')
        self.assertNotEqual(self.digest(), KNOWN_ANSWER)

    def test_empty_directories_and_the_excluded_names_are_ignored(self):
        base = self.digest()
        (self.root / 'another-empty' / 'nested').mkdir(parents=True)
        put(self.root, 'dir/__pycache__/y.cpython-312.pyc')
        put(self.root, 'dir/mod.pyc')
        put(self.root, 'dir/mod.pyo')
        put(self.root, 'deep/__pycache__/sub/z.txt')                         # anything inside a __pycache__ directory
        put(self.root, 'deep/.DS_Store')
        self.assertEqual(self.digest(), base)
        put(self.root, 'dir/mod.py')                                         # a look-alike is a real file
        self.assertNotEqual(self.digest(), base)

    def test_symlinks_that_leave_the_tree_or_are_absolute_are_refused(self):
        for label, target in (('outside', '../outside'), ('absolute', '/etc/passwd'), ('deep outside', 'dir/../../x'),
                              ('absolute inside', str(self.root / 'a.txt'))):
            with self.subTest(label):
                os.symlink(target, self.root / 'bad-link')
                with self.assertRaisesRegex(ValueError, '[Ss]ymlink'):
                    self.digest()
                os.unlink(self.root / 'bad-link')
        os.symlink('../dir/b.bin', self.root / 'dir' / 'ok-link')            # leaves its directory, not the tree
        self.digest()

    def test_a_dangling_link_that_stays_inside_the_tree_is_an_entry(self):
        os.symlink('no/such/file', self.root / 'dangling')
        self.assertIn('dangling', [e['path'] for e in subject.tree_digest(self.root)['entries']])

    def test_a_fifo_is_refused(self):
        os.mkfifo(self.root / 'pipe')
        with self.assertRaisesRegex(ValueError, 'not a regular file'):
            self.digest()

    def test_a_dot_git_entry_anywhere_is_refused(self):
        for label, make in (('directory', lambda p: p.mkdir()), ('file', lambda p: p.write_bytes(b'gitdir: x')),
                            ('link', lambda p: os.symlink('a.txt', p))):
            for where in ('.git', 'dir/.git'):
                with self.subTest(label=label, where=where):
                    make(self.root / where)
                    with self.assertRaisesRegex(ValueError, r'\.git'):
                        self.digest()
                    if (self.root / where).is_dir() and not (self.root / where).is_symlink():
                        (self.root / where).rmdir()
                    else:
                        os.unlink(self.root / where)

    def test_a_name_with_a_control_character_is_refused(self):
        for name in ('tab\tname', 'line\nbreak'):
            with self.subTest(repr(name)):
                put(self.root, name)
                with self.assertRaisesRegex(ValueError, 'control character'):
                    self.digest()
                os.unlink(self.root / name)

    def test_names_equal_under_case_folding_are_refused_in_a_name_list(self):
        subject.check_names(['a/b', 'a/c', 'B'])
        for names in (['README', 'readme'], ['dir/File.c', 'dir/file.c'], ['dir', 'DIR/x'], ['straße', 'STRASSE']):
            with self.subTest(names):
                with self.assertRaisesRegex(ValueError, 'case'):
                    subject.check_names(names)

    def test_names_equal_under_case_folding_are_refused_in_a_tree(self):
        put(self.root, 'Readme')
        try:
            (self.root / 'readme').write_bytes(b'other')
        except OSError:
            self.skipTest('this file system cannot hold two names that differ only in case')
        if len(list(self.root.glob('[Rr]eadme'))) != 2:
            self.skipTest('this file system is case-insensitive: SKIPPED, the name-list test above covers the rule')
        with self.assertRaisesRegex(ValueError, 'case'):
            self.digest()

    def test_a_name_that_is_not_utf8_is_refused_in_a_name_list(self):
        for raw in (b'bad\xff.c', b'dir/\xfe', b'\xc3'):
            with self.subTest(raw):
                with self.assertRaisesRegex(ValueError, 'UTF-8'):
                    subject.relative_name(raw)
        self.assertEqual(subject.relative_name('ünï/cødé.c'.encode()), 'ünï/cødé.c')

    def test_a_name_that_is_not_utf8_is_refused_in_a_tree(self):
        try:
            put(self.root, os.fsdecode(b'bad\xff.c'))
        except (OSError, UnicodeError):
            self.skipTest('this file system cannot hold a name that is not UTF-8: SKIPPED, the name-list test above covers the rule')
        with self.assertRaisesRegex(ValueError, 'UTF-8'):
            self.digest()

    def test_the_root_must_be_a_directory_and_not_a_link(self):
        with self.assertRaises(ValueError):
            subject.tree_digest(self.root / 'a.txt')
        with self.assertRaises(ValueError):
            subject.tree_digest(self.root / 'missing')
        os.symlink(self.root, Path(self.temp.name) / 'link-to-root')
        with self.assertRaises(ValueError):
            subject.tree_digest(Path(self.temp.name) / 'link-to-root')

    def test_an_unreadable_file_is_refused_not_skipped(self):
        if os.geteuid() == 0:
            self.skipTest('root reads any file')
        os.chmod(self.root / 'a.txt', 0)
        self.addCleanup(os.chmod, self.root / 'a.txt', 0o644)
        with self.assertRaises(ValueError):
            self.digest()

    def test_a_directory_symlink_is_one_entry_and_is_not_followed(self):
        put(self.root, 'real/x.c', b'real\n')
        os.symlink('real', self.root / 'alias')
        entries = {e['path']: e['kind'] for e in subject.tree_digest(self.root)['entries']}
        self.assertEqual(entries['alias'], 'symlink')
        self.assertNotIn('alias/x.c', entries)


class Marker(unittest.TestCase):
    """T4."""

    def test_for_one_patch_the_text_is_what_the_prepare_script_writes_byte_for_byte(self):
        text = subject.marker_text(PIN, [PATCH_ONE])
        self.assertEqual(text, '%s %s\n' % (PIN, PATCH_ONE))
        script = (ROOT / 'scripts' / 'prepare-wolf-tls-zephyr.sh').read_text()
        self.assertIn('''printf '%s %s\\n' "$zephyr_pin" "$patch_sha256" > "$output/.crux-wolf-tls"''', script)
        written = subprocess.run(['/bin/sh', '-c', 'printf "%s %s\\n" "$0" "$1"', PIN, PATCH_ONE], capture_output=True, check=True).stdout
        self.assertEqual(text.encode(), written)

    def test_for_two_patches_it_is_the_ordered_list(self):
        self.assertEqual(subject.marker_text(PIN, [PATCH_ONE, PATCH_TWO]), '%s %s %s\n' % (PIN, PATCH_ONE, PATCH_TWO))
        self.assertNotEqual(subject.marker_text(PIN, [PATCH_ONE, PATCH_TWO]), subject.marker_text(PIN, [PATCH_TWO, PATCH_ONE]))

    def test_the_guard_expects_the_same_text_for_the_one_patch_case(self):
        guard = (ROOT / 'tests' / 'zephyr' / 'management_rt1060_compile' / 'bench_guard.cmake').read_text()
        self.assertIn('"${marker_text}" STREQUAL "${BENCH_ZEPHYR_PIN} ${BENCH_PATCH_SHA256}"', guard)   # stripped text of the one-patch marker
        import json
        declaration = json.loads((ROOT / 'tests' / 'zephyr' / 'management_rt1060_compile' / 'bench_declaration.json').read_text())
        block = declaration['patched_zephyr']
        self.assertEqual(subject.marker_text(block['zephyr_pin'], [block['patch_sha256']]).rstrip('\n'),
                         '%s %s' % (block['zephyr_pin'], block['patch_sha256']))

    def test_a_marker_is_parsed_back_into_pin_and_hashes(self):
        self.assertEqual(subject.parse_marker(subject.marker_text(PIN, [PATCH_ONE, PATCH_TWO])), (PIN, [PATCH_ONE, PATCH_TWO]))
        self.assertEqual(subject.parse_marker('%s %s\n' % (PIN, PATCH_ONE)), (PIN, [PATCH_ONE]))

    def test_malformed_markers_are_refused(self):
        for text in ('', '\n', PIN + '\n', PIN + ' ' + PATCH_ONE, PIN + '  ' + PATCH_ONE + '\n', PIN + ' ' + PATCH_ONE + ' \n',
                     'short ' + PATCH_ONE + '\n', PIN + ' nothex\n', PIN + ' ' + PATCH_ONE + '\n\n', PIN + '\t' + PATCH_ONE + '\n'):
            with self.subTest(repr(text)), self.assertRaises(ValueError):
                subject.parse_marker(text)

    def test_the_text_builder_refuses_a_bad_pin_or_hash_or_an_empty_list(self):
        for pin, hashes in (('short', [PATCH_ONE]), (PIN, []), (PIN, ['nothex']), (PIN, [PATCH_ONE, PATCH_ONE])):
            with self.subTest(pin=pin, hashes=hashes), self.assertRaises(ValueError):
                subject.marker_text(pin, hashes)


def diff(path, kind='modified', extra=()):
    """A git-style diff section for ``path`` as ``git diff`` writes it."""
    header = ['diff --git a/%s b/%s' % (path, path)]
    if kind == 'added':
        header += ['new file mode 100644', 'index 0000000..1111111', '--- /dev/null', '+++ b/%s' % path, '@@ -0,0 +1 @@', '+new']
    elif kind == 'deleted':
        header += ['deleted file mode 100644', 'index 1111111..0000000', '--- a/%s' % path, '+++ /dev/null', '@@ -1 +0,0 @@', '-old']
    else:
        header += list(extra) + ['index 1111111..2222222', '--- a/%s' % path, '+++ b/%s' % path, '@@ -1 +1 @@', '-old', '+new']
    return '\n'.join(header) + '\n'


class PatchParsing(unittest.TestCase):
    """T5."""

    def parse(self, *sections, prefix=''):
        return subject.patch_touched_files((prefix + ''.join(sections)).encode())

    def test_added_modified_and_deleted_files_are_listed_in_order(self):
        got = self.parse(diff('b/mod.c'), diff('add.c', 'added'), diff('gone.c', 'deleted'))
        self.assertEqual(got, [('b/mod.c', 'modified'), ('add.c', 'added'), ('gone.c', 'deleted')])

    def test_text_before_the_first_header_is_ignored(self):
        mail = 'From abc\nSubject: [PATCH] x\n\ndiff --git is mentioned here but is no header\n---\n'
        self.assertEqual(self.parse(diff('x.c'), prefix=mail), [('x.c', 'modified')])

    def test_a_rename_a_copy_a_mode_change_and_a_binary_hunk_are_refused(self):
        rename = ('diff --git a/old.c b/new.c\nsimilarity index 100%\nrename from old.c\nrename to new.c\n')
        copy = 'diff --git a/old.c b/new.c\nsimilarity index 100%\ncopy from old.c\ncopy to new.c\n'
        mode = 'diff --git a/x.sh b/x.sh\nold mode 100644\nnew mode 100755\n'
        mode_with_content = diff('x.sh', extra=['old mode 100644', 'new mode 100755'])
        binary = 'diff --git a/x.bin b/x.bin\nindex 1111111..2222222 100644\nGIT binary patch\nliteral 1\nIc${x}\n\n'
        binary_files = 'diff --git a/x.bin b/x.bin\nindex 1111111..2222222 100644\nBinary files a/x.bin and b/x.bin differ\n'
        names_differ = 'diff --git a/one.c b/two.c\nindex 1111111..2222222 100644\n--- a/one.c\n+++ b/two.c\n@@ -1 +1 @@\n-a\n+b\n'
        for label, text in dict(rename=rename, copy=copy, mode=mode, mode_with_content=mode_with_content, binary=binary,
                                binary_files=binary_files, names_differ=names_differ).items():
            with self.subTest(label), self.assertRaises(ValueError):
                self.parse(text)

    def test_a_patch_that_touches_nothing_or_that_has_no_git_header_is_refused(self):
        plain = '--- a/x.c\n+++ b/x.c\n@@ -1 +1 @@\n-a\n+b\n'
        for label, text in dict(empty='', text='nothing here\n', plain_unified=plain).items():
            with self.subTest(label), self.assertRaises(ValueError):
                self.parse(text)

    def test_unsafe_or_quoted_paths_are_refused(self):
        for path in ('/abs.c', '../up.c', 'a/../b.c', 'a//b.c', 'a\\b.c', './x.c', '"quoted name".c', 'tab\tname.c', ''):
            with self.subTest(path), self.assertRaises(ValueError):
                self.parse(diff(path))

    def test_a_file_touched_twice_in_one_patch_is_refused(self):
        with self.assertRaises(ValueError):
            self.parse(diff('x.c'), diff('x.c'))

    def test_a_malformed_header_is_refused(self):
        for text in ('diff --git a/x.c\n', 'diff --git a/x.c b/x.c extra\n--- a/x.c\n', 'diff --git x.c x.c\n'):
            with self.subTest(text), self.assertRaises(ValueError):
                self.parse(text)

    def test_a_file_that_is_added_and_does_not_say_so_is_refused(self):
        # the dev-null side and the header must agree
        contradictory = 'diff --git a/x.c b/x.c\nindex 1111111..2222222 100644\n--- /dev/null\n+++ b/x.c\n@@ -0,0 +1 @@\n+n\n'
        with self.assertRaises(ValueError):
            self.parse(contradictory)

    def test_patch_bytes_that_are_not_utf8_are_parsed_by_their_header_lines_only(self):
        data = diff('x.c').encode() + b'+ \xff\xfe binary-looking content line\n'
        self.assertEqual(subject.patch_touched_files(data), [('x.c', 'modified')])

    def test_net_changes_of_a_list_of_patches(self):
        one = [('a.c', 'modified'), ('new.c', 'added'), ('gone.c', 'deleted')]
        two = [('a.c', 'modified'), ('b.c', 'modified'), ('new.c', 'modified')]
        net = subject.net_changes([('p1', one), ('p2', two)])
        self.assertEqual(net, [dict(path='a.c', change='modified', patches=['p1', 'p2']),
                               dict(path='b.c', change='modified', patches=['p2']),
                               dict(path='gone.c', change='deleted', patches=['p1']),
                               dict(path='new.c', change='added', patches=['p1', 'p2'])])

    def test_net_changes_follow_the_sequence_of_each_file(self):
        self.assertEqual(subject.net_changes([('p1', [('x.c', 'deleted')]), ('p2', [('x.c', 'added')])]),
                         [dict(path='x.c', change='modified', patches=['p1', 'p2'])])
        self.assertEqual(subject.net_changes([('p1', [('x.c', 'modified')]), ('p2', [('x.c', 'deleted')])]),
                         [dict(path='x.c', change='deleted', patches=['p1', 'p2'])])
        for label, sequence in dict(added_twice=[('x.c', 'added'), ('x.c', 'added')],
                                    modified_after_delete=[('x.c', 'deleted'), ('x.c', 'modified')],
                                    deleted_twice=[('x.c', 'deleted'), ('x.c', 'deleted')],
                                    added_then_deleted=[('x.c', 'added'), ('x.c', 'deleted')],
                                    added_after_modify=[('x.c', 'modified'), ('x.c', 'added')]).items():
            with self.subTest(label), self.assertRaises(ValueError):
                subject.net_changes([('p%d' % i, [item]) for i, item in enumerate(sequence)])

    def test_patch_names_are_plain_file_names(self):
        for name in ('zephyr-tls-4.4.1.patch', 'tcp-init-isn.patch', 'a_b+c.diff'):
            subject.check_patch_name(name)
        for name in ('', '.hidden.patch', 'a/b.patch', '../x.patch', 'sp ace.patch', 'nl\n.patch', 'ü.patch', '-x.patch'):
            with self.subTest(name), self.assertRaises(ValueError):
                subject.check_patch_name(name)


if __name__ == '__main__':
    unittest.main()
