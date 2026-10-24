"""Resolve the experimental overlay with the pinned board and devicetree parser."""
import importlib
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest


@unittest.skipUnless(os.environ.get('ZEPHYR_BASE'), 'requires pinned ZEPHYR_BASE')
class CandidateGeometry(unittest.TestCase):
    def test_file_scratch_and_nvs_are_exclusive_in_resolved_board(self):
        # A broad storage area or File/scratch overlap exposes the scratch sector.
        zephyr = Path(os.environ['ZEPHYR_BASE'])
        sys.path.insert(0, str(zephyr / 'scripts/dts/python-devicetree/src'))
        dtlib = importlib.import_module('devicetree.dtlib')
        board = zephyr / 'boards/nxp/mimxrt1060_evk/mimxrt1060_evk_mimxrt1062_qspi.dts'
        revision = board.with_name('mimxrt1060_evk_mimxrt1062_qspi_B.overlay')
        overlay = Path(__file__).resolve().parents[3] / 'samples/flash_target_candidate/app.overlay'
        with tempfile.TemporaryDirectory() as tmp:
            source, resolved = Path(tmp) / 'board.dts', Path(tmp) / 'board.dts.pre'
            source.write_text(f'#include "{board}"\n#include "{revision}"\n' +
                              (f'#include "{overlay}"\n' if overlay.exists() else ''))
            subprocess.run([os.environ.get('CC', 'cc'), '-E', '-P', '-x', 'assembler-with-cpp',
                            '-nostdinc', '-undef', '-D__DTS__',
                            '-I' + str(zephyr / 'include'), '-I' + str(zephyr / 'dts'),
                            '-I' + str(zephyr / 'dts/arm'), '-I' + str(zephyr / 'dts/common'),
                            '-I' + str(zephyr.parent / 'modules/hal/nxp/dts'),
                            str(source), '-o', str(resolved)], check=True)
            tree = dtlib.DT(str(resolved))
            for name, expected in [('candidate_file', (0x620000, 0x1cf000)),
                                   ('candidate_scratch', (0x7ef000, 0x1000)),
                                   ('candidate_nvs', (0x7f0000, 0x10000))]:
                self.assertTrue(name in tree.label2node, f'missing resolved owner: {name}')
                prop = tree.label2node[name].props['reg'].to_nums()
                self.assertEqual(tuple(prop), expected)
            flash = tree.label2node['is25wp064']
            self.assertEqual(tuple(flash.props['reg'].to_nums()), (0, 0x800000))
            # Every fixed partition is disjoint; none can retain broad storage ownership.
            partitions = flash.nodes['partitions']
            extents = sorted(tuple(node.props['reg'].to_nums()) for node in partitions.nodes.values())
            self.assertEqual(extents, [(0, 0x20000), (0x20000, 0x300000),
                                       (0x320000, 0x300000), (0x620000, 0x1cf000),
                                       (0x7ef000, 0x1000), (0x7f0000, 0x10000)])
            for (start, size), (next_start, _) in zip(extents, extents[1:]):
                self.assertEqual(start + size, next_start)
                self.assertEqual(start % 4096, 0)
                self.assertEqual(size % 4096, 0)
            self.assertEqual(sum(size for _, size in extents), 0x800000)


if __name__ == '__main__':
    unittest.main()
