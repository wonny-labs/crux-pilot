"""Negative controls prevent incomplete physical UART output being accepted."""
import importlib.util
import json
from pathlib import Path
import sys
import tempfile
import unittest
from unittest import mock

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "target"))
SPEC = importlib.util.spec_from_file_location(
    "capture_execution", Path(__file__).resolve().parents[1] / "target/capture_execution.py")
CAPTURE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(CAPTURE)


def controller():
    lines = ["*** Booting Zephyr OS build v4.4.2 ***", "controller composition=zephyr init=0"]
    for index in range(20):
        value = "unavailable" if index < 2 else str(1792238400 if index < 6 else 1792234800)
        lines.extend([f"cycle={index} status={-4 if index < 2 else 0} valid={int(index >= 2)} "
                      f"utc={value} monotonic_ms={index * 100} lateness_ms=0", "schedule skipped=0"])
    return "\n".join(lines + ["controller stack_bytes=4160 unused_bytes=3072 priority=5 heap_bytes=0",
                              "CONTROLLER_COMPLETE cycles=20"])


def native():
    names = ("bounded_messages", "lifecycle_periodic_and_stack",
             "mutex_contention_timeout_and_recovery", "semaphore_expiry_wakeup_and_capacity")
    lines = ["*** Booting Zephyr OS build v4.4.2 ***"]
    lines += [f"PASS - test_{name} in 0.1 seconds" for name in names]
    lines += [f"PROBE native cycle={index} lateness_ms=0" for index in range(6)]
    return "\n".join(lines + ["PROBE native periodic elapsed_ms=80 skipped=2 priority=5 stack_bytes=4160",
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

    def test_pre_reset_prefix_is_rejected(self):
        with self.assertRaises(AssertionError):
            CAPTURE.validate("stale output\n" + controller(), "controller")

    def test_wrong_physical_stack_object_size_is_rejected(self):
        with self.assertRaises(AssertionError):
            CAPTURE.validate(controller().replace("stack_bytes=4160", "stack_bytes=4096"), "controller")

    def manifest(self, root, config):
        (root / ".config").write_text(config)
        return {"artifacts": [{"path": str(root / "zephyr.bin")}, {"path": str(root / ".config")}]}

    def test_image_built_at_the_verified_level_is_accepted(self):
        with tempfile.TemporaryDirectory() as directory:
            CAPTURE.verify_optimization(self.manifest(
                Path(directory), 'CONFIG_SIZE_OPTIMIZATIONS=y\nCONFIG_COMPILER_OPT=""\n'))

    def test_debug_build_is_rejected(self):
        with tempfile.TemporaryDirectory() as directory:
            manifest = self.manifest(Path(directory), '# CONFIG_SIZE_OPTIMIZATIONS is not set\n'
                                     'CONFIG_DEBUG_OPTIMIZATIONS=y\nCONFIG_COMPILER_OPT=""\n')
            with self.assertRaises(ValueError):
                CAPTURE.verify_optimization(manifest)

    def test_manifest_without_one_configuration_is_rejected(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            for artifacts in ([], [{"path": str(root / "a/.config")}, {"path": str(root / "b/.config")}]):
                with self.assertRaises(AssertionError):
                    CAPTURE.verify_optimization({"artifacts": artifacts})

    def test_runner_rejects_a_debug_build_before_any_hardware_step(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            config = root / ".config"
            config.write_text('CONFIG_DEBUG_OPTIMIZATIONS=y\nCONFIG_COMPILER_OPT=""\n')
            manifest = root / "manifest.json"
            manifest.write_text(json.dumps({
                "target": "mimxrt1060_evk@B//qspi", "application": "native",
                "source": {"recoverable_from_git": True, "dirty": False},
                "artifacts": [{"path": str(config), "sha256": CAPTURE.sha(config)}]}))
            evidence = root / "evidence"
            argv = ["capture_execution.py", "--program", "--mode", "native",
                    "--uart", "/dev/cu.usbmodem11102", "--probe", "602003106",
                    "--board", "mimxrt1060_evk@B//qspi",
                    "--manifest", str(manifest), "--evidence", str(evidence)]
            with mock.patch.object(sys, "argv", argv), \
                    mock.patch.object(CAPTURE.platform, "system", return_value="Darwin"), \
                    mock.patch.object(CAPTURE.subprocess, "run") as run:
                with self.assertRaises(ValueError):
                    CAPTURE.main()
            run.assert_not_called()
            self.assertFalse(evidence.exists())


if __name__ == "__main__":
    unittest.main()
