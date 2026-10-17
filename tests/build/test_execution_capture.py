"""Negative controls prevent incomplete physical UART output being accepted."""
import importlib.util
from pathlib import Path
import unittest

SPEC = importlib.util.spec_from_file_location(
    "capture_execution", Path(__file__).resolve().parents[1] / "target/capture_execution.py")
CAPTURE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(CAPTURE)


def controller():
    lines = ["*** Booting Zephyr OS build v4.4.0 ***", "controller composition=zephyr init=0"]
    for index in range(20):
        value = "unavailable" if index < 2 else str(1792238400 if index < 6 else 1792234800)
        lines.extend([f"cycle={index} status={-4 if index < 2 else 0} valid={int(index >= 2)} "
                      f"utc={value} monotonic_ms={index * 100} lateness_ms=0", "schedule skipped=0"])
    return "\n".join(lines + ["controller stack_bytes=4096 unused_bytes=3072 priority=5 heap_bytes=0",
                              "CONTROLLER_COMPLETE cycles=20"])


def native():
    names = ("bounded_messages", "lifecycle_periodic_and_stack",
             "mutex_contention_timeout_and_recovery", "semaphore_expiry_wakeup_and_capacity")
    lines = ["*** Booting Zephyr OS build v4.4.0 ***"]
    lines += [f"PASS - test_{name} in 0.1 seconds" for name in names]
    lines += [f"PROBE native cycle={index} lateness_ms=0" for index in range(6)]
    return "\n".join(lines + ["PROBE native periodic elapsed_ms=80 skipped=2 priority=5 stack_bytes=4096",
                              "PROBE native stack_unused_bytes=3072",
                              "PROJECT EXECUTION SUCCESSFUL"])


class ExecutionCaptureTest(unittest.TestCase):
    def test_complete_native_suite_retains_selected_case_results(self):
        self.assertEqual(CAPTURE.validate(native(), "native")["native_cases"], 4)

    def test_native_missing_case_is_rejected(self):
        with self.assertRaises(AssertionError):
            CAPTURE.validate(native().replace("PASS - test_bounded_messages", "SKIP - case"), "native")

    def test_native_missing_overrun_evidence_is_rejected(self):
        with self.assertRaises(AssertionError):
            CAPTURE.validate(native().replace("skipped=2", "skipped=0"), "native")

    def test_complete_controller_has_twenty_cycles_and_stack_observation(self):
        result = CAPTURE.validate(controller(), "controller")
        self.assertEqual(result["cycles"], 20)
        self.assertEqual(result["used_bytes"], 1024)

    def test_missing_completion_is_rejected(self):
        with self.assertRaises(AssertionError):
            CAPTURE.validate(controller().replace("CONTROLLER_COMPLETE", "incomplete"), "controller")

    def test_duplicate_cycle_is_rejected(self):
        with self.assertRaises(AssertionError):
            CAPTURE.validate(controller().replace("cycle=19 ", "cycle=18 "), "controller")

    def test_inconsistent_monotonic_phase_is_rejected(self):
        with self.assertRaises(AssertionError):
            CAPTURE.validate(controller().replace("monotonic_ms=700", "monotonic_ms=701"), "controller")

    def test_missing_utc_step_is_rejected(self):
        with self.assertRaises(AssertionError):
            CAPTURE.validate(controller().replace("1792234800", "1792238400"), "controller")

    def test_insufficient_scoped_stack_headroom_is_rejected(self):
        with self.assertRaises(AssertionError):
            CAPTURE.validate(controller().replace("unused_bytes=3072", "unused_bytes=1000"), "controller")

    def test_repeated_boot_is_rejected(self):
        with self.assertRaises(AssertionError):
            CAPTURE.validate("*** Booting Zephyr\n" + controller(), "controller")


if __name__ == "__main__":
    unittest.main()
