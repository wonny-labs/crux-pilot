import sys
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "target"))

import unittest
from m3_health_oracle import validate


def fixture():
    rows = [(0., '*** Booting Zephyr OS build v4.4.0 ***'),
            (0.01, 'M3_BOOT source=0123456789ab profile=independent-health commands=1 fault_default=off'),
            (0.02, 'M3_RESET support_status=0 supported=0x1f cause_status=0 cause=0x1 clear_status=0 remaining_status=0 remaining=0x0'),
            (2.05, 'M3_WDT_ARMED timeout_ms=2000 debug_pause=1 freshness_ms=500')]
    for ms in range(2100, 15000, 100):
        if ms == 14500:
            rows.append((ms / 1000, 'M3_FAULT_STALL monotonic_ms=14500 sequence=145'))
        completed = min(ms, 14400)
        if ms <= 14900:
            rows.append((ms / 1000 + .01, f'M3_WDT_FEED monotonic_ms={ms} sequence={completed // 100} completed_ms={completed}'))
    rows.extend([(15.01, 'M3_HEALTH_STALE monotonic_ms=15000 sequence=144'),
                 (16., 'M3_OBSERVER_ALIVE monotonic_ms=16000 healthy=0 sequence=144 readiness=0'),
                 (17., '*** Booting Zephyr OS build v4.4.0 ***'),
                 (17.01, 'M3_BOOT source=0123456789ab profile=independent-health commands=1 fault_default=off'),
                 (17.02, 'M3_RESET support_status=0 supported=0x1f cause_status=0 cause=0x10 clear_status=0 remaining_status=0 remaining=0x0')])
    for n in range(1, 5):
        rows.append((17.1 + n, f'M3_OBSERVER_ALIVE monotonic_ms={1000*n} healthy=1 sequence={10*n} readiness=0'))
    rows.extend([(1.01, 'M3_OBSERVER_ALIVE monotonic_ms=1000 healthy=1 sequence=11 readiness=0'), (2.01, 'M3_OBSERVER_ALIVE monotonic_ms=2000 healthy=1 sequence=21 readiness=0')])
    for ms in range(0, 15000, 1000):
        rows.append((ms / 1000 + .04, f'M3_WORK sequence={ms // 100 + 1} monotonic_ms={ms} time=-4 pressed=0 device=0'))
    rows += [(t+.0001, 'M3_STACK worker_status=0 worker_object=4160 worker_usable=4096 worker_unused=3000 main_status=0 main_usable=2048 main_unused=1500') for t, s in rows if s.startswith('M3_OBSERVER_ALIVE ')]
    return sorted(rows, key=lambda row: row[0])


class WatchdogOracleTest(unittest.TestCase):
    def check(self, rows):
        return validate(rows, '0123456789ab')

    def test_complete_scoped_fixture(self):
        self.assertEqual(self.check(fixture())['status'], 'PASS')

    def test_negative_controls(self):
        variants = [
            lambda rows: rows[:-4],
            lambda rows: [(t, s) for t, s in rows if not s.startswith('M3_STACK ')],
            lambda rows: [(t, s.replace('healthy=1', 'healthy=0') if 19 < t < 20 and s.startswith('M3_OBSERVER_ALIVE ') else s) for t, s in rows],
            lambda rows: [(t, s.replace('monotonic_ms=14500', 'monotonic_ms=14000')) for t, s in rows],
            lambda rows: [(t + .5 if 8 < t < 9 else t, s) for t, s in rows],
            lambda rows: [(t, s) for t, s in rows if not (s.startswith('M3_RESET ') and t < 3)],
            lambda rows: [(t, s) for t, s in rows if not (s.startswith('M3_WORK ') and t < 3)],
            lambda rows: [(t, s) for t, s in rows if not (s.startswith('M3_OBSERVER_ALIVE ') and t < 3)],
            lambda rows: [(-1., 'stale prefix')] + rows,
            lambda rows: [(t, s.replace('cause=0x10', 'cause=0x1')) for t, s in rows],
            lambda rows: [(t, s.replace('remaining=0x0', 'remaining=0x10')) for t, s in rows],
            lambda rows: [(t, s.replace('readiness=0', 'readiness=1')) for t, s in rows],
            lambda rows: [(t, s.replace('fault_default=off', 'fault_default=on')) for t, s in rows],
            lambda rows: [(t, s.replace('worker_unused=3000', 'worker_unused=1000')) for t, s in rows],
            lambda rows: [(t, s.replace('main_unused=1500', 'main_unused=500')) for t, s in rows],
            lambda rows: [(t, s.replace('worker_object=4160', 'worker_object=4096')) for t, s in rows],
            lambda rows: [(t, s.replace('completed_ms=14000', 'completed_ms=13000')) for t, s in rows],
            lambda rows: [(t, s) for t, s in rows if not s.startswith('M3_FAULT_STALL ')],
            lambda rows: [(t, s) for t, s in rows if not s.startswith('M3_HEALTH_STALE ')],
            lambda rows: [(t, s) for t, s in rows if 'healthy=0' not in s],
            lambda rows: rows + [(22., 'M3_BOOT source=0123456789ab profile=independent-health commands=1 fault_default=off')],
            lambda rows: [(t - 2 if s.startswith('M3_FAULT_STALL ') else t, s) for t, s in rows],
            lambda rows: [(t - 5 if s.startswith('M3_WDT_ARMED ') else t, s) for t, s in rows],
        ]
        for index, mutate in enumerate(variants):
            with self.subTest(index=index), self.assertRaises(ValueError):
                self.check(mutate(fixture()))


if __name__ == '__main__':
    unittest.main()
