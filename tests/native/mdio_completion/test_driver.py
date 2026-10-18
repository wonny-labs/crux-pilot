#!/usr/bin/env python3
"""Check the generated pinned driver with explicit logical IRQ schedules."""
import hashlib
import importlib.util
import os
from pathlib import Path
import re
import subprocess
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[3]
ZEPHYR = Path(os.environ.get("ZEPHYR_BASE", ROOT.parent / "zephyr"))
SPEC = importlib.util.spec_from_file_location("prepare_mdio", ROOT / "scripts/prepare-nxp-mdio.py")
PREPARE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(PREPARE)


def definition(text, declaration):
    start = text.index(declaration)
    end = text.index("{", start) + 1
    depth = 1
    while depth:
        depth += (text[end] == "{") - (text[end] == "}")
        end += 1
    return text[start:end]


class GeneratedDriverTests(unittest.TestCase):
    def test_actual_deferred_completion_observer(self):
        with tempfile.TemporaryDirectory() as directory:
            work = Path(directory)
            headers = work / "zephyr"
            (headers / "logging").mkdir(parents=True)
            (headers / "device.h").write_text(
                '#pragma once\n#include <stdbool.h>\nstruct device { int unused; };\n'
                'const struct device *observer_device_get(int node);\n'
                'bool device_is_ready(const struct device *dev);\n'
                '#define DEVICE_DT_GET(node) observer_device_get(node)\n')
            (headers / "devicetree.h").write_text(
                '#define DT_NODELABEL(name) 1\n#define DT_PARENT(node) 2\n')
            (headers / "kernel.h").write_text(
                '#pragma once\n#include <stdint.h>\n#include <stddef.h>\n'
                'struct k_thread { const char *name; struct {size_t size;} stack_info; };\n'
                'typedef struct k_thread *k_tid_t;\n'
                'int printk(const char *format, ...);\nint64_t k_uptime_get(void);\n'
                'const char *k_thread_name_get(k_tid_t thread);\n'
                'void k_thread_foreach(void (*callback)(const struct k_thread *, void *), void *);\n'
                'int k_thread_stack_space_get(const struct k_thread *, size_t *);\n'
                'int k_thread_priority_get(k_tid_t thread);\n')
            (headers / "logging/log_ctrl.h").write_text(
                '#include <stdint.h>\nuint32_t log_buffered_cnt(void);\n')
            sample = ROOT / "samples/controller/zephyr"
            subprocess.run([os.environ.get("CC", "cc"), "-std=c17", "-Wall", "-Wextra", "-Werror",
                            "-DCONFIG_CRUX_M3_DEFERRED_OUTPUT=1", "-DCONFIG_LOG_PROCESS_THREAD_STACK_SIZE=2048",
                            "-I", str(work), "-I", str(sample), str(sample / "network_mdio_trace.c"),
                            str(Path(__file__).with_name("deferred_observer_model.c")),
                            "-o", str(work / "observer")], check=True)
            for scenario in ([], ["failure"], ["late"], ["duplicate"]):
                subprocess.run([str(work / "observer")] + scenario, check=True)

    def test_actual_observer_quiet_until_retained_failure(self):
        with tempfile.TemporaryDirectory() as directory:
            work = Path(directory)
            headers = work / "zephyr"
            headers.mkdir()
            (headers / "device.h").write_text(
                '#pragma once\n#include <stdbool.h>\nstruct device { int unused; };\n'
                'const struct device *observer_device_get(int node);\n'
                'bool device_is_ready(const struct device *dev);\n'
                '#define DEVICE_DT_GET(node) observer_device_get(node)\n')
            (headers / "devicetree.h").write_text(
                '#define DT_NODELABEL(name) 1\n#define DT_PARENT(node) 2\n')
            (headers / "kernel.h").write_text('int printk(const char *format, ...);\n')
            sample = ROOT / "samples/controller/zephyr"
            subprocess.run([os.environ.get("CC", "cc"), "-std=c17", "-Wall", "-Wextra", "-Werror",
                            "-I", str(work), "-I", str(sample), str(sample / "network_mdio_trace.c"),
                            str(Path(__file__).with_name("observer_model.c")),
                            "-o", str(work / "observer")], check=True)
            subprocess.run([str(work / "observer")], check=True)

    def test_actual_first_failure_record(self):
        with tempfile.TemporaryDirectory() as directory:
            work = Path(directory)
            generated = work / "driver.c"
            PREPARE.prepare(ZEPHYR, generated)
            text = generated.read_text()
            (work / "candidate_fragments.inc").write_text("\n".join(
                definition(text, "static int " + name + "(")
                for name in ("nxp_enet_mdio_wait_xfer", "mdio_transfer")))
            (work / "driver_data.inc").write_text(definition(text, "struct nxp_enet_mdio_data {") + ";\n")
            (work / "trace_functions.inc").write_text(
                definition(text, "static void nxp_enet_mdio_capture_failure(") + "\n"
                + definition(text, "bool crux_m3_mdio_failure_get("))
            (work / "driver_functions.inc").write_text((work / "trace_functions.inc").read_text()
                                                        + "\n" + (work / "candidate_fragments.inc").read_text())
            header = ZEPHYR.parent / "modules/hal/nxp/mcux/mcux-sdk-ng/devices/RT/RT1060/periph/PERI_ENET.h"
            macros = [line for line in header.read_text().splitlines() if re.match(
                r"#define ENET_(EIR_MII_MASK|MMFR_(ST|OP|PA|RA|TA|DATA)(\(|_(MASK|SHIFT)))", line)]
            (work / "register_macros.inc").write_text("\n".join(macros) + "\n")
            subprocess.run([os.environ.get("CC", "cc"), "-std=c17", "-Wall", "-Wextra", "-Werror",
                            "-DCONFIG_CRUX_M3_MDIO_FAILURE_TRACE=1", "-I", str(work),
                            "-I", str(ROOT / "samples/controller/zephyr"),
                            str(Path(__file__).with_name("trace_model.c")), "-o", str(work / "trace_model")], check=True)
            subprocess.run([str(work / "trace_model")], check=True)

    def test_model_failure_invalidates_previous_m3_manifest(self):
        with tempfile.TemporaryDirectory() as directory:
            work = Path(directory)
            repository = work / "fixture"
            evidence = repository / "build/evidence"
            evidence.mkdir(parents=True)
            for name in ("m3-inputs.json", "m3-inputs.after.json"):
                (evidence / name).write_text("stale success")
            scripts = repository / "scripts"
            scripts.mkdir()
            (scripts / "record-build-provenance.py").write_text(
                'import pathlib,sys\npathlib.Path(sys.argv[-1]).write_text("pending inputs")\n')
            model = repository / "tests/native/mdio_completion/test_driver.py"
            model.parent.mkdir(parents=True)
            model.write_text('raise SystemExit(23)\n')
            commands = work / "bin"
            commands.mkdir()
            for name, body in (("git", 'printf "%s\\n" "$CRUX_TEST_REPO"'),
                               ("uname", 'printf "Linux\\n"')):
                executable = commands / name
                executable.write_text("#!/bin/sh\n" + body + "\n")
                executable.chmod(0o755)
            env = dict(os.environ, CRUX_TEST_REPO=str(repository), CRUX_WORKSPACE=str(work),
                       PATH=str(commands) + os.pathsep + os.environ["PATH"])
            result = subprocess.run(["bash", str(ROOT / "scripts/test-m3.sh")], env=env,
                                    capture_output=True, text=True)
            self.assertEqual(result.returncode, 23, result.stdout + result.stderr)
            self.assertFalse((evidence / "m3-inputs.json").exists())
            self.assertFalse((evidence / "m3-inputs.after.json").exists())
            self.assertEqual((evidence / "m3-inputs.pending.json").read_text(), "pending inputs")

    def test_missing_or_duplicate_original_source_rejected(self):
        with tempfile.TemporaryDirectory() as directory:
            work = Path(directory)
            for count in (0, 2):
                project = work / str(count)
                project.mkdir()
                source = ZEPHYR / PREPARE.RELATIVE_SOURCE
                listing = " ".join('"' + str(source) + '"' for _ in range(count))
                (project / "CMakeLists.txt").write_text(
                    'cmake_minimum_required(VERSION 3.20)\nproject(mdio_negative C)\n'
                    'add_library(drivers__ethernet STATIC)\n'
                    + ('target_sources(drivers__ethernet PRIVATE ' + listing + ')\n' if count else '')
                    + 'set(ZEPHYR_BASE "' + str(ZEPHYR) + '")\n'
                    + 'set(PYTHON_EXECUTABLE "' + os.sys.executable + '")\n'
                    + 'include("' + str(ROOT / 'samples/controller/zephyr/mdio_completion.cmake') + '")\n')
                result = subprocess.run(['cmake', '-S', str(project), '-B', str(project / 'build')],
                                        capture_output=True, text=True)
                self.assertNotEqual(result.returncode, 0)
                self.assertIn('Expected exactly one pinned MDIO source, found ' + str(count),
                              result.stdout + result.stderr)

    def test_actual_candidate_schedules(self):
        with tempfile.TemporaryDirectory() as directory:
            work = Path(directory)
            generated = work / "driver.c"
            PREPARE.prepare(ZEPHYR, generated)
            text = generated.read_text()
            self.assertEqual(hashlib.sha256(generated.read_bytes()).hexdigest(), PREPARE.PATCHED_SHA256)
            (work / "candidate_fragments.inc").write_text("\n".join(
                definition(text, "static int " + name + "(")
                for name in ("nxp_enet_mdio_wait_xfer", "mdio_transfer")))
            (work / "driver_data.inc").write_text(definition(text, "struct nxp_enet_mdio_data {") + ";\n")
            header = ZEPHYR.parent / "modules/hal/nxp/mcux/mcux-sdk-ng/devices/RT/RT1060/periph/PERI_ENET.h"
            macros = [line for line in header.read_text().splitlines() if re.match(
                r"#define ENET_(EIR_MII_MASK|MMFR_(ST|OP|PA|RA|TA|DATA)(\(|_(MASK|SHIFT)))", line)]
            (work / "register_macros.inc").write_text("\n".join(macros) + "\n")
            subprocess.run([os.environ.get("CC", "cc"), "-std=c17", "-Wall", "-Wextra", "-Werror", "-I", str(work),
                            str(Path(__file__).with_name("model.c")), "-o", str(work / "model")], check=True)
            subprocess.run([str(work / "model")], check=True)

    def test_upstream_drift_rejected_without_output(self):
        with tempfile.TemporaryDirectory() as directory:
            work = Path(directory)
            source = work / "zephyr" / PREPARE.RELATIVE_SOURCE
            source.parent.mkdir(parents=True)
            source.write_bytes((ZEPHYR / PREPARE.RELATIVE_SOURCE).read_bytes() + b"\n")
            output = work / "generated.c"
            with self.assertRaisesRegex(ValueError, "Unsupported upstream"):
                PREPARE.prepare(work / "zephyr", output)
            self.assertFalse(output.exists())

    def test_output_in_dependency_rejected(self):
        with self.assertRaisesRegex(ValueError, "outside the Zephyr"):
            PREPARE.prepare(ZEPHYR, ZEPHYR / "must-not-be-written.c")

    def test_unexpected_patch_result_rejected_without_output(self):
        with tempfile.TemporaryDirectory() as directory:
            work = Path(directory)
            modified = work / "changed.patch"
            modified.write_text(PREPARE.PATCH.read_text().replace("until a new boot", "until another boot"))
            output = work / "generated.c"
            with patch.object(PREPARE, "PATCH", modified):
                with self.assertRaisesRegex(ValueError, "Unexpected patched"):
                    PREPARE.prepare(ZEPHYR, output)
            self.assertFalse(output.exists())


if __name__ == "__main__":
    unittest.main()
