#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Tests of scripts/power_cut.py with fake commands. No hardware, no hub, no probe.

The wrapper is the --cut-cmd of the power-cut host scripts: it switches a port off, waits the
planned pause, reads the target voltage and exits non-zero unless the reading is about 0 V. Each
case builds the off and read commands from small Python one-liners, so what the wrapper does with
a good reading, a high reading, a failed off command and an unreadable reading is checked without
a board.
"""
import json
import os
import shlex
import subprocess
import sys
import tempfile
import time
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
SCRIPT = os.path.join(HERE, "..", "..", "scripts", "power_cut.py")


def py(code):
    """A command line (one string) that runs the given Python code."""
    return shlex.join([sys.executable, "-c", code])


class PowerCutTest(unittest.TestCase):
    def setUp(self):
        self.dir = tempfile.TemporaryDirectory()
        self.addCleanup(self.dir.cleanup)
        self.log = os.path.join(self.dir.name, "cut.log")
        self.marker = os.path.join(self.dir.name, "read-ran")

    def run_wrapper(self, off, read, pause="0.05", max_mv="300", read_timeout="2", log=None):
        argv = [sys.executable, SCRIPT, "--off-cmd", off, "--read-cmd", read, "--pause", pause,
                "--max-mv", max_mv, "--read-timeout", read_timeout,
                "--log", self.log if log is None else log]
        return subprocess.run(argv, capture_output=True, text=True, timeout=30)

    def records(self):
        with open(self.log, encoding="utf-8") as handle:
            return [json.loads(entry) for entry in handle if entry.strip()]

    def test_low_reading_passes_and_is_logged(self):
        result = self.run_wrapper(py("pass"), py("print(12)"))
        self.assertEqual(result.returncode, 0, result.stderr)
        [record] = self.records()
        self.assertEqual(record["verdict"], "power_removed")
        self.assertEqual(record["mv"], 12)
        self.assertEqual(record["max_mv"], 300)
        self.assertEqual(record["off_rc"], 0)
        for key in ("off_start_utc", "off_returned_utc", "read_start_utc", "read_end_utc"):
            self.assertIn(key, record)

    def test_reading_at_the_limit_passes_and_one_above_fails(self):
        self.assertEqual(self.run_wrapper(py("pass"), py("print(300)")).returncode, 0)
        self.assertEqual(self.run_wrapper(py("pass"), py("print(301)")).returncode, 1)

    def test_supply_still_present_exits_1(self):
        result = self.run_wrapper(py("pass"), py("print(3322)"))
        self.assertEqual(result.returncode, 1)
        self.assertEqual(self.records()[-1]["verdict"], "power_present")
        self.assertEqual(self.records()[-1]["mv"], 3322)

    def test_failed_off_command_exits_3_and_never_reads(self):
        read = py(f"open({self.marker!r}, 'w').close(); print(0)")
        result = self.run_wrapper(py("import sys; sys.exit(7)"), read)
        self.assertEqual(result.returncode, 3)
        self.assertFalse(os.path.exists(self.marker))
        record = self.records()[-1]
        self.assertEqual(record["verdict"], "off_failed")
        self.assertEqual(record["off_rc"], 7)

    def test_missing_off_command_exits_3(self):
        result = self.run_wrapper("/nonexistent/uhubctl -a off", py("print(0)"))
        self.assertEqual(result.returncode, 3)
        self.assertEqual(self.records()[-1]["verdict"], "off_failed")

    def test_unreadable_values_exit_4(self):
        for text in ("nothing", "", "0.000 V", "-5", "12 13", "0x10"):
            with self.subTest(text=text):
                result = self.run_wrapper(py(f"print({text!r})"), py(f"print({text!r})"))
                # The off command printing the same text does not matter: only the reader is judged.
                self.assertEqual(result.returncode, 4)
                self.assertEqual(self.records()[-1]["verdict"], "read_failed")

    def test_reader_with_unit_suffix_is_accepted(self):
        self.assertEqual(self.run_wrapper(py("pass"), py("print('0 mV')")).returncode, 0)
        self.assertEqual(self.run_wrapper(py("pass"), py("print('3322mV')")).returncode, 1)

    def test_last_nonempty_line_of_the_reader_is_the_reading(self):
        result = self.run_wrapper(py("pass"), py("print('probe 1'); print(8); print()"))
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(self.records()[-1]["mv"], 8)

    def test_reader_failure_exit_status_is_a_read_failure_even_with_a_low_value(self):
        result = self.run_wrapper(py("pass"), py("print(0); import sys; sys.exit(1)"))
        self.assertEqual(result.returncode, 4)

    def test_reader_that_hangs_is_a_read_failure(self):
        started = time.monotonic()
        result = self.run_wrapper(py("pass"), py("import time; time.sleep(30)"),
                                  read_timeout="0.5")
        self.assertEqual(result.returncode, 4)
        self.assertLess(time.monotonic() - started, 10)
        self.assertEqual(self.records()[-1]["verdict"], "read_failed")

    def test_the_reading_is_taken_at_the_end_of_the_pause(self):
        result = self.run_wrapper(py("pass"), py("print(0)"), pause="0.6")
        self.assertEqual(result.returncode, 0, result.stderr)
        record = self.records()[-1]
        self.assertGreaterEqual(record["read_start_monotonic"] - record["off_returned_monotonic"],
                                0.6 - 0.01)
        self.assertAlmostEqual(record["pause_s"], 0.6)

    def test_pause_counts_from_the_return_of_the_off_command(self):
        slow_off = py("import time; time.sleep(0.4)")
        result = self.run_wrapper(slow_off, py("print(0)"), pause="0.3")
        self.assertEqual(result.returncode, 0, result.stderr)
        record = self.records()[-1]
        self.assertGreaterEqual(record["off_returned_monotonic"] - record["off_start_monotonic"],
                                0.39)
        self.assertGreaterEqual(record["read_start_monotonic"] - record["off_returned_monotonic"],
                                0.3 - 0.01)

    def test_log_is_appended_one_json_line_per_cut(self):
        self.run_wrapper(py("pass"), py("print(0)"))
        self.run_wrapper(py("pass"), py("print(3300)"))
        self.assertEqual([r["verdict"] for r in self.records()], ["power_removed", "power_present"])

    def test_unwritable_log_exits_5_even_when_the_power_is_off(self):
        result = self.run_wrapper(py("pass"), py("print(0)"),
                                  log=os.path.join(self.dir.name, "missing", "cut.log"))
        self.assertEqual(result.returncode, 5)

    def test_summary_line_goes_to_stderr_and_names_the_verdict_and_the_reading(self):
        result = self.run_wrapper(py("pass"), py("print(21)"))
        self.assertEqual(result.stdout, "")
        self.assertIn("power_removed", result.stderr)
        self.assertIn("21", result.stderr)

    def test_every_option_is_required(self):
        base = ["--off-cmd", py("pass"), "--read-cmd", py("print(0)"), "--pause", "0.05",
                "--max-mv", "300", "--read-timeout", "2", "--log", self.log]
        for index in range(0, len(base), 2):
            with self.subTest(option=base[index]):
                argv = [sys.executable, SCRIPT] + base[:index] + base[index + 2:]
                result = subprocess.run(argv, capture_output=True, text=True, timeout=30)
                self.assertEqual(result.returncode, 2)

    def test_negative_or_zero_numbers_are_refused(self):
        for option, value in (("pause", "-1"), ("max_mv", "-1"), ("read_timeout", "0")):
            with self.subTest(option=option):
                result = self.run_wrapper(py("pass"), py("print(0)"), **{option: value})
                self.assertEqual(result.returncode, 2)

    def test_non_finite_and_huge_numbers_are_refused_before_anything_runs(self):
        off = py(f"open({self.marker!r}, 'w').close()")
        for option in ("pause", "read_timeout"):
            for value in ("nan", "inf", "-inf", "1e300", "61"):
                with self.subTest(option=option, value=value):
                    result = self.run_wrapper(off, py("print(0)"), **{option: value})
                    self.assertEqual(result.returncode, 2)
                    self.assertFalse(os.path.exists(self.marker))

    def test_the_largest_accepted_pause_and_timeout_are_60_seconds(self):
        # Parsed only: a wrapper that waited 60 s would make this test slow, so use the parser.
        sys.path.insert(0, os.path.join(HERE, "..", "..", "scripts"))
        import power_cut
        args = power_cut.build_parser().parse_args(
            ["--off-cmd", "x", "--read-cmd", "y", "--pause", "60", "--max-mv", "0",
             "--read-timeout", "60", "--log", "z"])
        self.assertEqual((args.pause, args.read_timeout), (60.0, 60.0))

    def test_empty_or_unparsable_commands_are_refused_before_anything_runs(self):
        for text in ("", "   ", "'unbalanced"):
            for option in ("off", "read"):
                with self.subTest(option=option, text=text):
                    kwargs = {option: text}
                    other = py(f"open({self.marker!r}, 'w').close(); print(0)")
                    kwargs.setdefault("off", other)
                    kwargs.setdefault("read", other)
                    result = self.run_wrapper(**kwargs)
                    self.assertEqual(result.returncode, 2)
                    self.assertFalse(os.path.exists(self.marker))

    def test_missing_reader_binary_is_a_read_failure(self):
        result = self.run_wrapper(py("pass"), "/nonexistent/reader --mv")
        self.assertEqual(result.returncode, 4)
        self.assertEqual(self.records()[-1]["verdict"], "read_failed")

    def test_failed_off_record_has_no_reading_fields(self):
        self.run_wrapper(py("import sys; sys.exit(1)"), py("print(0)"))
        record = self.records()[-1]
        self.assertNotIn("read_start_utc", record)
        self.assertIsNone(record["mv"])

    def test_off_command_that_hangs_is_an_off_failure(self):
        sys.path.insert(0, os.path.join(HERE, "..", "..", "scripts"))
        import power_cut
        original = power_cut.OFF_TIMEOUT_S
        power_cut.OFF_TIMEOUT_S = 0.5
        self.addCleanup(setattr, power_cut, "OFF_TIMEOUT_S", original)
        status = power_cut.main(["--off-cmd", py("import time; time.sleep(30)"),
                                 "--read-cmd", py("print(0)"), "--pause", "0",
                                 "--max-mv", "300", "--read-timeout", "2", "--log", self.log])
        self.assertEqual(status, 3)
        self.assertEqual(self.records()[-1]["verdict"], "off_failed")

    def test_an_unexpected_error_after_the_cut_is_logged_and_exits_6_not_1(self):
        sys.path.insert(0, os.path.join(HERE, "..", "..", "scripts"))
        import power_cut
        original = power_cut.parse_reading
        def boom(_output):
            raise RuntimeError("simulated")
        # parse_reading is reached by the wrapper only, so no library call can hit the patch.
        power_cut.parse_reading = boom
        self.addCleanup(setattr, power_cut, "parse_reading", original)
        status = power_cut.main(["--off-cmd", py("pass"), "--read-cmd", py("print(0)"),
                                 "--pause", "0", "--max-mv", "300", "--read-timeout", "2",
                                 "--log", self.log])
        self.assertEqual(status, 6)
        record = self.records()[-1]
        self.assertEqual(record["verdict"], "wrapper_error")
        self.assertIn("simulated", record["error"])

    def test_wrapper_is_callable_through_a_shell_as_the_host_scripts_do(self):
        inner = " ".join(shlex.quote(part) for part in (
            sys.executable, SCRIPT, "--off-cmd", py("pass"), "--read-cmd", py("print(5)"),
            "--pause", "0.05", "--max-mv", "300", "--read-timeout", "2", "--log", self.log))
        done = subprocess.run(inner, shell=True, capture_output=True, text=True, timeout=30)
        self.assertEqual(done.returncode, 0, done.stderr)
        self.assertEqual(self.records()[-1]["mv"], 5)


if __name__ == "__main__":
    unittest.main()
