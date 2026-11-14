"""The opt-in warning of the four frozen public headers, and how Crux keeps it out of its own build.

``CRUX_WARN_FROZEN_HEADERS`` is set by a product, never by Crux. ``CRUX_INTERNAL_BUILD`` is set by
Crux's own CMake directories, as a directory compile definition, so it reaches no consumer.
"""
import json
import os
from pathlib import Path
import platform
import re
import shutil
import subprocess
import tempfile
import unittest

REPO = Path(__file__).resolve().parents[2]
CC = shutil.which(os.environ.get("CC", "cc"))
CMAKE = shutil.which("cmake")
FROZEN = ("thread", "sync", "queue", "serial")
INCLUDES = sorted(str(path) for path in REPO.glob("components/*/include"))
DOCS_POINTER = "docs/choosing-an-api.md"


def preprocess(source, *flags):
    """Preprocess one translation unit. Return (exit status, diagnostics)."""
    with tempfile.TemporaryDirectory() as tmp:
        unit = Path(tmp) / "unit.c"
        unit.write_text(source)
        command = [CC, "-std=c17", "-E", "-o", os.devnull, *flags, str(unit)]
        for include in INCLUDES:
            command.append(f"-I{include}")
        result = subprocess.run(command, capture_output=True, text=True)
    return result.returncode, result.stderr


@unittest.skipUnless(CC, "a C compiler is required")
class FrozenHeaderWarningTest(unittest.TestCase):
    def test_by_default_each_header_is_silent_even_with_warnings_as_errors(self):
        for name in FROZEN:
            with self.subTest(header=name):
                status, diagnostics = preprocess(f"#include <crux/{name}.h>\n", "-Wall", "-Wextra",
                                                 "-Wpedantic", "-Werror")
                self.assertEqual((0, ""), (status, diagnostics))

    def test_when_opted_in_each_header_warns_and_names_itself_and_the_guide(self):
        for name in FROZEN:
            with self.subTest(header=name):
                status, diagnostics = preprocess(f"#include <crux/{name}.h>\n", "-DCRUX_WARN_FROZEN_HEADERS")
                self.assertEqual(0, status, diagnostics)
                self.assertRegex(diagnostics, r"warning")
                self.assertIn(f"<crux/{name}.h>", diagnostics)
                self.assertIn(DOCS_POINTER, diagnostics)

    def test_the_warning_is_one_per_header_per_translation_unit(self):
        _status, diagnostics = preprocess("#include <crux/sync.h>\n#include <crux/sync.h>\n",
                                          "-DCRUX_WARN_FROZEN_HEADERS")
        self.assertEqual(1, len(re.findall(r"warning:", diagnostics)), diagnostics)

    def test_an_opted_in_product_can_make_it_an_error(self):
        for name in FROZEN:
            with self.subTest(header=name):
                status, diagnostics = preprocess(f"#include <crux/{name}.h>\n",
                                                 "-DCRUX_WARN_FROZEN_HEADERS", "-Werror")
                self.assertNotEqual(0, status, diagnostics)

    def test_any_value_of_the_opt_in_macro_enables_it(self):
        for value in ("CRUX_WARN_FROZEN_HEADERS=1", "CRUX_WARN_FROZEN_HEADERS=0", "CRUX_WARN_FROZEN_HEADERS="):
            with self.subTest(value=value):
                _status, diagnostics = preprocess("#include <crux/queue.h>\n", f"-D{value}")
                self.assertIn(DOCS_POINTER, diagnostics)

    def test_inside_crux_the_opt_in_stays_silent(self):
        for name in FROZEN:
            with self.subTest(header=name):
                status, diagnostics = preprocess(
                    f"#include <crux/{name}.h>\n", "-DCRUX_WARN_FROZEN_HEADERS", "-DCRUX_INTERNAL_BUILD",
                    "-Werror")
                self.assertEqual((0, ""), (status, diagnostics))

    def test_the_other_public_headers_never_warn_when_opted_in(self):
        others = sorted(path for path in REPO.glob("components/*/include/crux/*.h")
                        if path.stem not in FROZEN)
        self.assertTrue(others)
        for header in others:
            with self.subTest(header=header.name):
                _status, diagnostics = preprocess(f"#include <crux/{header.name}>\n",
                                                  "-DCRUX_WARN_FROZEN_HEADERS")
                self.assertNotIn(DOCS_POINTER, diagnostics)

    def test_the_header_text_names_the_macros_and_keeps_the_warning_inside_its_include_guard(self):
        for name in FROZEN:
            with self.subTest(header=name):
                text = next(REPO.glob(f"components/{name}/include/crux/{name}.h")).read_text()
                guard = f"CRUX_{name.upper()}_H"
                block = re.search(r"#if defined\(CRUX_WARN_FROZEN_HEADERS\) && !defined\(CRUX_INTERNAL_BUILD\)"
                                  r"\n#warning [^\n]+\n#endif\n", text)
                self.assertIsNotNone(block)
                self.assertGreater(text.index(block.group()), text.index(f"#define {guard}"))
                self.assertLess(text.index(block.group()), text.index("extern \"C\""))


@unittest.skipUnless(CMAKE, "CMake is required")
class InternalBuildDefinitionTest(unittest.TestCase):
    """Crux's own sources compile with CRUX_INTERNAL_BUILD, and no consumer inherits it."""

    PROBE = r"""
cmake_minimum_required(VERSION 3.28)
project(probe LANGUAGES C)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CRUX_BUILD_TIME ON CACHE BOOL "")
set(CRUX_BUILD_THREAD ON CACHE BOOL "")
set(CRUX_BUILD_SYNC ON CACHE BOOL "")
set(CRUX_BUILD_QUEUE ON CACHE BOOL "")
set(CRUX_BUILD_SERIAL ON CACHE BOOL "")
set(CRUX_TIME_PROVIDER memory CACHE STRING "")
add_subdirectory("@REPO@" crux)
add_library(product STATIC product.c)
target_link_libraries(product PRIVATE crux::thread crux::sync crux::queue crux::serial)
foreach(target IN ITEMS crux_thread_host crux_sync_host crux_queue_linux crux_serial_linux crux_thread_api
                        crux_sync_api crux_serial_api crux_queue product)
    if(TARGET ${target})
        get_target_property(usage ${target} INTERFACE_COMPILE_DEFINITIONS)
        message(STATUS "USAGE ${target} [${usage}]")
    endif()
endforeach()
"""

    def configure(self, *flags):
        tmp = tempfile.TemporaryDirectory()
        self.addCleanup(tmp.cleanup)
        root = Path(tmp.name)
        (root / "CMakeLists.txt").write_text(self.PROBE.replace("@REPO@", str(REPO)))
        (root / "product.c").write_text("#include <crux/thread.h>\nint product_value;\n")
        result = subprocess.run([CMAKE, "-S", str(root), "-B", str(root / "build"), *flags],
                                capture_output=True, text=True)
        return root, result

    @unittest.skipUnless(platform.system() == "Linux", "the Host providers configure on Linux only")
    def test_crux_sources_get_it_and_the_consumer_source_does_not(self):
        root, result = self.configure()
        self.assertEqual(0, result.returncode, result.stdout + result.stderr)
        commands = json.loads((root / "build/compile_commands.json").read_text())
        crux = [c for c in commands if f"{REPO}/components/" in c["file"]]
        product = [c for c in commands if c["file"].endswith("/product.c")]
        self.assertGreaterEqual(len(crux), 4)
        self.assertEqual(1, len(product))
        for entry in crux:
            with self.subTest(file=entry["file"]):
                self.assertIn("-DCRUX_INTERNAL_BUILD", entry["command"])
        self.assertNotIn("CRUX_INTERNAL_BUILD", product[0]["command"])

    @unittest.skipUnless(platform.system() == "Linux", "the Host providers configure on Linux only")
    def test_no_target_exports_it_as_a_usage_requirement(self):
        _root, result = self.configure()
        self.assertEqual(0, result.returncode, result.stdout + result.stderr)
        seen = re.findall(r"USAGE (\w+) \[([^\]]*)\]", result.stdout)
        self.assertGreaterEqual(len(seen), 5)
        for target, usage in seen:
            with self.subTest(target=target):
                self.assertNotIn("CRUX_INTERNAL_BUILD", usage)

    def test_the_zephyr_module_sets_it_as_a_directory_definition_too(self):
        text = (REPO / "zephyr/CMakeLists.txt").read_text()
        self.assertRegex(text, r"(?m)^add_compile_definitions\(CRUX_INTERNAL_BUILD\)$")

    def test_the_root_sets_it_as_a_directory_definition_before_any_component(self):
        text = (REPO / "CMakeLists.txt").read_text()
        self.assertRegex(text, r"(?m)^add_compile_definitions\(CRUX_INTERNAL_BUILD\)$")
        self.assertLess(text.index("add_compile_definitions(CRUX_INTERNAL_BUILD)"),
                        text.index("add_subdirectory(components/base)"))

    def test_nothing_exports_the_define_as_usage_requirement(self):
        paths = [REPO / "CMakeLists.txt", REPO / "zephyr/CMakeLists.txt",
                 *REPO.glob("components/**/CMakeLists.txt"), *REPO.glob("cmake/*.cmake")]
        for path in paths:
            with self.subTest(path=path.relative_to(REPO).as_posix()):
                text = path.read_text()
                for call in re.findall(r"target_compile_definitions\([^)]*\)", text, re.DOTALL):
                    if "CRUX_INTERNAL_BUILD" in call:
                        self.assertNotRegex(call, r"\b(INTERFACE|PUBLIC)\b")
                self.assertNotRegex(text, r"INTERFACE_COMPILE_DEFINITIONS[^\n]*CRUX_INTERNAL_BUILD")

    @unittest.skipUnless(platform.system() == "Linux", "the Host providers build on Linux only")
    def test_a_product_opting_in_globally_warns_only_in_its_own_file(self):
        root, result = self.configure("-DCMAKE_C_FLAGS=-DCRUX_WARN_FROZEN_HEADERS")
        self.assertEqual(0, result.returncode, result.stdout + result.stderr)
        build = subprocess.run([CMAKE, "--build", str(root / "build"), "--target", "product", "crux_thread_host",
                                "crux_sync_host", "crux_queue_linux", "crux_serial_linux"],
                               capture_output=True, text=True)
        self.assertEqual(0, build.returncode, build.stdout + build.stderr)
        lines = (build.stdout + build.stderr).splitlines()
        warnings = [index for index, line in enumerate(lines) if "warning:" in line]
        self.assertEqual(1, len(warnings), lines)
        self.assertIn("product.c", lines[warnings[0] - 1])  # "In file included from .../product.c"
        self.assertIn(DOCS_POINTER, lines[warnings[0]])


class GuideTest(unittest.TestCase):
    def test_the_guide_names_the_macro_the_scan_and_the_second_layer(self):
        text = (REPO / "docs/choosing-an-api.md").read_text()
        section = text[text.index("## Frozen for new product code"):text.index("## Coexistence in one image")]
        self.assertIn("CRUX_WARN_FROZEN_HEADERS", section)
        self.assertIn("check_domain_includes.py", section)
        self.assertIn("--product", section)
        self.assertNotIn("Nothing checks the rest of the product's code", section)


if __name__ == "__main__":
    unittest.main()
