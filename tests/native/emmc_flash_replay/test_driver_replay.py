#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Tests of the driver replay harness. They need no build: traces are made from the models."""
import os
import sys
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.join(HERE, '..', 'emmc_trailer'))
sys.path.insert(0, os.path.join(HERE, '..', 'emmc_powercut'))

from replay_model import SECTOR  # noqa: E402
from run_driver_replay import (SIM_BYTES, compare_with_model, driver_args,  # noqa: E402
                               evaluate, sim_baseline, trace_from_driver)
from write_path import DEVICES, LayoutConfig, encode_trace, run_scenario  # noqa: E402

CONFIG = LayoutConfig.sector()


def model_raw(path, padded=False, policy='flush'):
    trace = run_scenario(path, CONFIG, policy=policy, padded=padded)
    return trace, encode_trace(trace.events)


class DriverReplayHarnessTest(unittest.TestCase):
    def test_a_trace_equal_to_the_models_has_no_difference_and_the_same_calls_and_marks(self):
        for path in ('confirm', 'revert'):
            for padded in (False, True):
                model, raw = model_raw(path, padded)
                driver = trace_from_driver(raw, sim_baseline(CONFIG), CONFIG, path, padded)
                self.assertEqual(compare_with_model(driver, path, padded, CONFIG), [])
                self.assertEqual(driver.marks, model.marks)
                self.assertEqual(driver.starts, model.starts)
                self.assertEqual(driver.calls, model.calls)

    def test_a_different_trace_is_reported(self):
        _, raw = model_raw('confirm')
        broken = bytearray(raw)
        broken[-40] ^= 0x01  # inside the last record
        driver = trace_from_driver(bytes(broken), sim_baseline(CONFIG), CONFIG, 'confirm', False)
        self.assertTrue(compare_with_model(driver, 'confirm', False, CONFIG))
        _, shorter = model_raw('confirm')
        cut = shorter[:-16 * 2]  # without the last flush and the last mark
        driver = trace_from_driver(cut, sim_baseline(CONFIG), CONFIG, 'confirm', False)
        self.assertTrue(compare_with_model(driver, 'confirm', False, CONFIG))

    def test_baseline_fills_the_simulator_medium_and_keeps_slot_a(self):
        baseline = sim_baseline(CONFIG)
        self.assertEqual(len(baseline), SIM_BYTES)
        self.assertEqual(baseline[CONFIG.slot_size:CONFIG.slot_size + 64], bytes(64))
        self.assertEqual(baseline[CONFIG.slot_size + CONFIG.slot_size:], bytes(
            SIM_BYTES - 2 * CONFIG.slot_size))
        self.assertEqual(baseline[0:4], bytes([0x3d, 0xb8, 0xf3, 0x96]), 'the image magic')

    def test_evaluate_holds_every_property_with_flushes_and_fails_without(self):
        for path in ('confirm', 'revert'):
            _, raw = model_raw(path)
            driver = trace_from_driver(raw, sim_baseline(CONFIG), CONFIG, path, False)
            outcome = evaluate(driver, seeds=range(2))
            for device in DEVICES:
                failures = outcome[device.name]['failures']
                # P7 belongs to the tear models and fails by design under a tear of a native unit.
                self.assertEqual({p: n for p, n in failures.items() if p != 'P7'},
                                 {p: 0 for p in failures if p != 'P7'}, device.name)
        _, raw = model_raw('confirm', policy='none')
        driver = trace_from_driver(raw, sim_baseline(CONFIG), CONFIG, 'confirm', False)
        outcome = evaluate(driver, seeds=range(2))
        self.assertTrue(outcome['fifo-barrier']['failures']['P3'])
        self.assertEqual(outcome['fifo-barrier']['failures']['P1'], 0)

    def test_arguments_for_the_program_carry_the_image_and_the_magic(self):
        args = driver_args('confirm', False, CONFIG, '/x/img', '/x/trace')
        self.assertIn('--emmc-image=/x/img', args)
        self.assertIn('--emmc-trace=/x/trace', args)
        self.assertIn('--workload=confirm', args)
        self.assertTrue(any(a.startswith('--image-hex=') and len(a) > 6000 for a in args))
        magic = [a for a in args if a.startswith('--magic-hex=')][0].split('=')[1]
        self.assertEqual(len(magic), 32)
        self.assertEqual(magic[:4], '0002', 'the alignment prefix of a 512-byte maximum')
        self.assertIn('--workload=confirm-pad', driver_args('confirm', True, CONFIG, 'a', 'b'))


if __name__ == '__main__':
    unittest.main()
