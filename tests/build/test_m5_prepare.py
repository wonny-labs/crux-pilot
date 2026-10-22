"""Frozen M5 software image evidence rejects stale identity and tampering."""
import hashlib
import json
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch

TARGET = Path(__file__).resolve().parents[1] / 'target'
sys.path.insert(0, str(TARGET))
import prepare_m5_network as subject


class Preparation(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.build = self.root / 'build'
        folder = self.build / 'zephyr'
        folder.mkdir(parents=True)
        self.source = dict(git_commit='a' * 40, dirty=False, recoverable_from_git=True,
                           snapshot_sha256='b' * 64)
        for name in ('zephyr.elf', 'zephyr.bin', '.config', 'zephyr.map', 'zephyr.dts'):
            (folder / name).write_bytes(b'a' * 12 + name.encode())
        (self.build / 'compile_commands.json').write_text('[]')
        self.inputs = self.root / 'inputs.json'
        self.inputs.write_text(json.dumps(dict(source=self.source, west_projects=[
            dict(name=k, revision=v, git_commit=v, dirty=False)
            for k, v in subject.PINNED.items()])))
        provenance = json.loads(self.inputs.read_text())
        provenance['artifacts'] = [dict(path=str(p.relative_to(self.root)), size_bytes=p.stat().st_size,
                                       sha256=hashlib.sha256(p.read_bytes()).hexdigest())
                                   for p in folder.iterdir()]
        (self.build / 'provenance.json').write_text(json.dumps(provenance))
        self.output = self.root / 'manifest.json'
        self.repo = self.root / 'repo'
        self.repo.mkdir()
        (self.repo / 'tool.py').write_text('frozen tool')
        self.patches = [patch.object(subject, 'source_snapshot', return_value=self.source),
                        patch.object(subject, 'check_image', return_value={'geometry': 'checked'}),
                        patch.object(subject, 'prepare_errno', side_effect=self.errno),
                        patch.object(subject, 'verify_errno_map', return_value={'EIO': 5})]
        for mocked in self.patches:
            mocked.start()
            self.addCleanup(mocked.stop)

    def errno(self, build, inputs, output, **kwargs):
        self.assertEqual(kwargs, {'source_suffix': '/transaction.c'})
        output.mkdir()
        (output / 'errno-map.json').write_text('{}')
        (output / 'compiler-executable.bin').write_bytes(b'actual compiler')

    def prepare(self):
        return subject.prepare(self.build, self.inputs, self.output, self.root,
                               tool_paths=[self.repo / 'tool.py'])

    def verify(self):
        return subject.verify(self.output, subject.sha(self.output))

    def test_preserves_clean_workspace_manifest_context_separately_from_built_source(self):
        context = dict(name='manifest', path='crux-pilot', revision='HEAD',
                       git_commit='1d6ac4f6cd5b4e4013f2ce0c59375beb3cdad9f2', dirty=False)
        for path in (self.inputs, self.build / 'provenance.json'):
            data = json.loads(path.read_text())
            data['west_projects'].insert(0, context)
            path.write_text(json.dumps(data))
        record = self.prepare()
        self.assertEqual(record['west_projects'][0], context)
        self.assertEqual(self.verify()['west_projects'][0], context)
        self.assertEqual(record['source'], self.source)
        self.assertNotEqual(record['source']['git_commit'], context['git_commit'])

    def test_rejects_noncanonical_workspace_context_and_unknown_or_duplicate_projects(self):
        baseline = json.loads(self.inputs.read_text())
        context = dict(name='manifest', path='crux-pilot', revision='HEAD',
                       git_commit='1d6ac4f6cd5b4e4013f2ce0c59375beb3cdad9f2', dirty=False)
        mutations = [dict(context, dirty=True), dict(context, path='../crux-pilot'),
                     dict(context, path='other-checkout'), dict(context, revision='main'),
                     dict(context, git_commit='not-a-commit'), dict(context, name='unknown')]
        for changed in mutations:
            data = dict(baseline, west_projects=[changed] + baseline['west_projects'])
            with self.subTest(context=changed), self.assertRaises(ValueError):
                subject.frozen_inputs(data)
        for extra in ([context, context], [baseline['west_projects'][0]]):
            data = dict(baseline, west_projects=extra + baseline['west_projects'])
            with self.subTest(extra=extra), self.assertRaises(ValueError):
                subject.frozen_inputs(data)

    def test_freezes_all_artifacts_and_compiler_and_verifies_read_only(self):
        record = self.prepare()
        names = {Path(a['path']).name for a in record['artifacts']}
        self.assertTrue({'zephyr.elf', 'zephyr.bin', '.config', 'zephyr.map', 'zephyr.dts',
                         'compile_commands.json', 'inputs.json', 'compiler-executable.bin',
                         'errno-map.json', 'tool.py'} <= names)
        self.assertEqual(self.verify()['source'], self.source)
        with self.assertRaises(FileExistsError):
            self.prepare()

    def test_rejects_dirty_stale_source_dependency_and_missing_banner(self):
        for change in ('dirty', 'source', 'dependency', 'banner'):
            with self.subTest(change=change):
                data = json.loads(self.inputs.read_text())
                if change == 'dirty':
                    data['source']['dirty'] = True
                elif change == 'source':
                    data['source']['snapshot_sha256'] = 'c' * 64
                elif change == 'dependency':
                    data['west_projects'][0]['git_commit'] = 'd' * 40
                else:
                    (self.build / 'zephyr/zephyr.bin').write_bytes(b'no source banner')
                original = self.inputs.read_text()
                self.inputs.write_text(json.dumps(data))
                with self.assertRaises(ValueError):
                    self.prepare()
                self.inputs.write_text(original)

    def test_rejects_each_changed_frozen_artifact_and_wrong_manifest_digest(self):
        record = self.prepare()
        with self.assertRaises(ValueError):
            subject.verify(self.output, '0' * 64)
        for artifact in record['artifacts']:
            path = Path(artifact['path'])
            original = path.read_bytes()
            path.write_bytes(original + b'changed')
            with self.subTest(path=path.name), self.assertRaises(ValueError):
                self.verify()
            path.write_bytes(original)

    def test_rejects_duplicate_missing_and_incomplete_evidence(self):
        record = self.prepare()
        original = self.output.read_text()
        for change in ('duplicate', 'missing', 'incomplete'):
            data = json.loads(original)
            if change == 'duplicate':
                data['artifacts'].append(data['artifacts'][0])
            elif change == 'missing':
                data['artifacts'] = data['artifacts'][1:]
            else:
                del data['source']
            self.output.write_text(json.dumps(data))
            with self.subTest(change=change), self.assertRaises((ValueError, KeyError)):
                self.verify()
        self.output.write_text(original)
