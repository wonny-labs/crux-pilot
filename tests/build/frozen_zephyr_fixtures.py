"""A frozen patched-Zephyr folder built by hand, without git, tar or patch (design B2, tests T7 and T10).

The manifest block, the record and the frozen artefacts are written by plain Python in the schema of
``patched_zephyr.freeze``, independently of the freeze code, so the image check is tested on any host. A test that has the real
programs compares the shape of this fixture with the record of a real freeze (see test_bench_patched_zephyr_check.py), so the
two cannot drift apart. The patches are synthetic and are NOT the wolfSSL patch or the second (``tcp_init_isn``) patch.
"""
import hashlib
import json
from pathlib import Path
import sys

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE.parent / 'target'))
import patched_zephyr

MARKER_FILE = '.crux-wolf-tls'
REAL_PIN = 'dccb09599635bdff17633fa7e9dab014b91dce90'          # bench_image.PINNED['zephyr'], pinned by a test
KEEP, OMIT = object(), object()


def sha(data):
    return hashlib.sha256(data).hexdigest()


class Frozen:
    """Two patches: ``a.c`` and ``new.c`` are touched by both, ``b.c`` by the second only. ``count=1`` gives today's single patch."""

    def __init__(self, root, count=2):
        self.root = Path(root)
        self.build = self.root / 'build'
        self.folder = self.build / 'patched-zephyr'
        self.folder.mkdir(parents=True)
        self.patch_bytes = {'first-1.patch': b'synthetic patch one\n', 'first-2.patch': b'synthetic patch two\n'}
        self.names = list(self.patch_bytes)[:count]
        stock = {'subsys/a.c': b'a stock\n', 'subsys/b.c': b'b stock\n'}
        final = {'subsys/a.c': b'a final\n', 'subsys/b.c': b'b final\n', 'subsys/new.c': b'new final\n'}
        touched = {'subsys/a.c': ('modified', self.names), 'subsys/b.c': ('modified', self.names[-1:]),
                   'subsys/new.c': ('added', self.names)}
        if count == 1:
            touched = {'subsys/a.c': ('modified', self.names), 'subsys/new.c': ('added', self.names)}
        files = [dict(path=path, change=touched[path][0], sha256=sha(final[path]), patches=list(touched[path][1]))
                 for path in sorted(touched)]
        self.block = dict(schema=2, decision='D8', marker_file=MARKER_FILE, pin=REAL_PIN,
                          patches=[dict(name=n, sha256=sha(self.patch_bytes[n])) for n in self.names], patched_files=files,
                          tree=dict(algorithm='crux-zephyr-tree-v1', sha256=sha(b'copy tree'), file_count=7))
        marker = '%s %s\n' % (REAL_PIN, ' '.join(p['sha256'] for p in self.block['patches']))
        patches = [dict(name=n, **self.put('patches/' + n, self.patch_bytes[n])) for n in self.names]
        patched = []
        for item in files:
            self.put('files/' + item['path'], final[item['path']])
            patched.append(dict(item, stock_sha256=sha(stock[item['path']]) if item['change'] == 'modified' else None,
                                frozen_path='files/' + item['path'], size_bytes=len(final[item['path']])))
        summary = dict(sha256=self.block['tree']['sha256'], file_count=7, symlink_count=1)
        self.record = dict(
            schema=1, algorithm='crux-zephyr-tree-v1',
            excluded=dict(names=['.DS_Store'], directories=['__pycache__'], suffixes=['.pyc', '.pyo']),
            pin=REAL_PIN, marker=dict(text=marker, **self.put('marker', marker.encode())),
            tree=dict(pre=summary, post=dict(summary), stock=dict(sha256=sha(b'stock tree'), file_count=5, symlink_count=1),
                      rederived=dict(summary)),
            patches=patches, patched_files=patched, tree_listing=self.put('tree-files.tsv', b'listing\n'),
            compile_commands=dict(translation_units=3, sources_under_copy=2, sources_under_stock=0),
            manifest_block_sha256=patched_zephyr.block_sha256(self.block))
        self.manifest = self.build / 'bench-manifest.json'
        self.record_path = self.folder / 'patched-zephyr.json'
        self.write_manifest()
        self.write_record()

    def put(self, relative, data):
        path = self.folder / relative
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(data)
        return dict(path=relative, sha256=sha(data), size_bytes=len(data))

    def write_manifest(self, block=KEEP):
        """``block`` defaults to the declared block, ``None`` writes a null, ``OMIT`` leaves the key out."""
        manifest = dict(image_id='crux-mgmt-bench-rt1060-evkb-b')
        if block is not OMIT:
            manifest['patched_zephyr'] = self.block if block is KEEP else block
        self.manifest.write_text(json.dumps(manifest, indent=2, sort_keys=True) + '\n')

    def write_record(self, record=None):
        self.record_path.write_text(json.dumps(self.record if record is None else record, indent=2, sort_keys=True) + '\n')

    def edit_record(self, change):
        """Change the record on disk with ``change(record)`` and write it; the manifest block hash is kept."""
        record = json.loads(self.record_path.read_text())
        change(record)
        self.write_record(record)

    def edit_block(self, change, resign=False):
        """Change a deep copy of the manifest block; ``resign`` also updates the block hash in the record."""
        block = json.loads(json.dumps(self.block))
        change(block)
        self.write_manifest(block)
        if resign:
            self.edit_record(lambda r: r.update(manifest_block_sha256=patched_zephyr.block_sha256(block)))

    def check(self, expected_pin=REAL_PIN):
        return patched_zephyr.check_record(self.manifest, self.record_path, expected_pin)
