"""Boot-phase decision of the M5 physical fixture can never re-arm the watchdog by accident."""
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

FIXTURE = Path(__file__).resolve().parents[1] / 'm5_physical_serial_system' / 'zephyr'
ARM, REPORT, OBSERVE = 0, 1, 2
NONE_, ARMED, DONE = 0, 1, 2
GARBAGE = 0x7777
CASES = [
    # (marker valid, marker phase, cause readable, WATCHDOG cause latched) -> phase
    ((0, 0, 1, 0), ARM), ((0, ARMED, 1, 0), ARM), ((0, DONE, 1, 0), ARM),
    ((1, NONE_, 1, 0), ARM), ((1, ARMED, 1, 0), REPORT), ((1, DONE, 1, 0), OBSERVE),
    ((1, GARBAGE, 1, 0), OBSERVE), ((1, 0xffffffff, 1, 0), OBSERVE),
    # Never arm blind: an unreadable cause only observes.
    ((0, 0, 0, 0), OBSERVE), ((1, NONE_, 0, 0), OBSERVE), ((1, ARMED, 0, 0), OBSERVE),
    # A lost marker with the WATCHDOG cause latched must report, never arm again.
    ((0, 0, 1, 1), REPORT), ((1, NONE_, 1, 1), REPORT), ((1, ARMED, 1, 1), REPORT),
    ((1, DONE, 1, 1), OBSERVE), ((1, GARBAGE, 1, 1), OBSERVE),
]

HARNESS = r'''
#include <stdio.h>
#include "phase.h"
int main(void)
{
    struct { int valid; unsigned phase; int known; int wd; } cases[] = {%s};
    for (unsigned i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i)
        printf("%%d\n", (int)m5_decide_phase(cases[i].valid, cases[i].phase, cases[i].known, cases[i].wd));
    printf("%%u %%u\n", m5_marker_after_arm(1), m5_marker_after_arm(0));
    return 0;
}
'''


class PhaseLogic(unittest.TestCase):
    def run_harness(self):
        compiler = shutil.which('cc') or shutil.which('gcc')
        self.assertTrue(compiler, 'no C compiler')
        rows = ', '.join('{%d, %du, %d, %d}' % (v, p, k, w) for (v, p, k, w), _ in CASES)
        with tempfile.TemporaryDirectory() as root:
            source = Path(root) / 'harness.c'
            source.write_text(HARNESS % rows)
            binary = Path(root) / 'harness'
            subprocess.run([compiler, '-std=c17', '-Wall', '-Wextra', '-Werror', '-I', str(FIXTURE),
                            str(source), '-o', str(binary)], check=True)
            out = subprocess.run([str(binary)], check=True, capture_output=True, text=True).stdout
        return out.split('\n')

    def test_decisions(self):
        lines = self.run_harness()
        for index, (case, expected) in enumerate(CASES):
            self.assertEqual(int(lines[index]), expected, case)

    def test_arm_requires_readable_cause_without_watchdog_bit(self):
        lines = self.run_harness()
        for index, ((valid, phase, known, wd), _) in enumerate(CASES):
            if int(lines[index]) == ARM:
                self.assertEqual((known, wd), (1, 0), CASES[index])

    def test_marker_after_arm_only_when_watchdog_started(self):
        lines = self.run_harness()
        started, failed = map(int, lines[len(CASES)].split())
        self.assertEqual(started, ARMED)
        self.assertEqual(failed, DONE)  # never retry after an uncertain start


if __name__ == '__main__':
    unittest.main()
