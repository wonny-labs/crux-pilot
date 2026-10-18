import os
from pathlib import Path
import subprocess
import sys
import unittest
from unittest.mock import patch
TARGET = Path(__file__).resolve().parents[1] / 'target'
sys.path.insert(0, str(TARGET))
import m3_clock


class SharedClock(unittest.TestCase):
    def test_actual_child_clock_is_bracketed_by_parent(self):
        environment = dict(os.environ, PYTHONPATH=str(TARGET))
        before = m3_clock.monotonic()
        result = subprocess.run([sys.executable, '-c', 'import m3_clock;print(m3_clock.monotonic())'],
                                env=environment, capture_output=True, text=True, timeout=3)
        after = m3_clock.monotonic()
        self.assertEqual(result.returncode, 0)
        self.assertEqual(result.stderr, '')
        self.assertLessEqual(before, float(result.stdout))
        self.assertLessEqual(float(result.stdout), after)

    def test_unsupported_clock_fails_without_process_relative_fallback(self):
        with patch.object(m3_clock._native, 'clock_gettime', side_effect=OSError('unsupported')):
            with self.assertRaises(OSError):
                m3_clock.monotonic()


if __name__ == '__main__': unittest.main()
