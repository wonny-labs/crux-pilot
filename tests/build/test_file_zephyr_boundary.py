"""The native-free consumer check rejects a consumer that links the File composition target.

`crux::file_zephyr` is a composition interface, not a portable usage target. These checks configure small
CMake projects on the host with the real guard (cmake/CruxConsumerBoundary.cmake) and the real definition of
the target, taken from components/file/ports/zephyr/CMakeLists.txt, without a Zephyr SDK. The same projects
configure cleanly against a weakened copy of the guard, so a passing rejection is not vacuous.
"""
from pathlib import Path
import re
import shutil
import subprocess
import tempfile
import unittest

REPO = Path(__file__).resolve().parents[2]
CMAKE = shutil.which("cmake")
GUARD = REPO / "cmake/CruxConsumerBoundary.cmake"
FILE_COMPONENT = REPO / "components/file"
PORT_CMAKE = FILE_COMPONENT / "ports/zephyr/CMakeLists.txt"
REJECTION = "native consumer requirement"

# The mock needs only what the real function reads: the component directory and the File target it links.
# The function text is the real one, so a change to what the target exposes changes this test's input.
_FUNCTION = re.search(r"function\(crux_file_zephyr_composition\).*?endfunction\(\)", PORT_CMAKE.read_text(),
                      re.S)

PROJECT = f"""cmake_minimum_required(VERSION 3.28)
project(probe LANGUAGES C)
include("@GUARD@")
set(CRUX_FILE_COMPONENT_DIR "{FILE_COMPONENT}")
add_library(crux_file_api INTERFACE)
target_include_directories(crux_file_api INTERFACE "{FILE_COMPONENT}/include")
add_library(crux_file INTERFACE)
add_library(crux::file ALIAS crux_file)
target_link_libraries(crux_file INTERFACE crux_file_api)
{_FUNCTION.group(0) if _FUNCTION else ""}
crux_file_zephyr_composition()
file(WRITE "${{CMAKE_BINARY_DIR}}/main.c" "int main(void) {{ return 0; }}\\n")
"""

# What a consumer might do with the target.
DIRECT_LINK = """add_executable(app "${CMAKE_BINARY_DIR}/main.c")
target_link_libraries(app PRIVATE crux::file crux::file_zephyr)
crux_assert_portable_consumer(app)
"""
USAGE_OF_THE_TARGET = "crux_assert_portable_usage(crux::file_zephyr)\n"
USAGE_THROUGH_A_WRAPPER = """add_library(product_files INTERFACE)
target_link_libraries(product_files INTERFACE crux::file_zephyr)
add_library(product::files ALIAS product_files)
add_executable(app "${CMAKE_BINARY_DIR}/main.c")
target_link_libraries(app PRIVATE product::files)
crux_assert_portable_usage(product::files)
"""
SCENARIOS = {"a direct link of a consumer": DIRECT_LINK,
             "the usage check of the target itself": USAGE_OF_THE_TARGET,
             "a transitive wrapper": USAGE_THROUGH_A_WRAPPER}
# The portable File target alone must pass, so the rejection is about the composition target.
PORTABLE_CONSUMER = """add_executable(app "${CMAKE_BINARY_DIR}/main.c")
target_link_libraries(app PRIVATE crux::file)
crux_assert_portable_consumer(app)
crux_assert_portable_usage(crux::file)
"""


def weaken_demoting_the_error(text):
    return text.replace("message(FATAL_ERROR \"native consumer requirement", "message(STATUS \"native consumer requirement")


def weaken_dropping_the_zephyr_patterns(text):
    # Removes the two tests that name Zephyr: the token and the component port directories.
    text = text.replace("(zephyr|boards?|soc|", "(boards?|soc|")
    return text.replace('               OR lower MATCHES "(^|/)components/[^/]+/(src|ports|host|zephyr|tests?)(/|$)"\n', "")


WEAKENINGS = {"the error is demoted to a status line": weaken_demoting_the_error,
              "the Zephyr name and port directory patterns are dropped": weaken_dropping_the_zephyr_patterns}


@unittest.skipUnless(CMAKE and _FUNCTION, "CMake and the target definition are required")
class ComposeTargetIsRejected(unittest.TestCase):
    def configure(self, body, guard=GUARD):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            (root / "CMakeLists.txt").write_text(PROJECT.replace("@GUARD@", str(guard)) + body)
            result = subprocess.run([CMAKE, "-S", str(root), "-B", str(root / "build")],
                                    capture_output=True, text=True)
        return result, result.stdout + result.stderr

    def test_the_mock_carries_the_real_definition_of_the_target(self):
        result, output = self.configure("")
        self.assertEqual(result.returncode, 0, output)
        for needle in ("ports/zephyr/include", "INTERFACE crux_file"):
            self.assertIn(needle, _FUNCTION.group(0))

    def test_a_portable_consumer_of_file_passes(self):
        result, output = self.configure(PORTABLE_CONSUMER)
        self.assertEqual(result.returncode, 0, output)
        self.assertNotIn(REJECTION, output)

    def test_the_guard_rejects_every_use_of_the_composition_target(self):
        for name, body in SCENARIOS.items():
            with self.subTest(scenario=name):
                result, output = self.configure(body)
                self.assertNotEqual(result.returncode, 0, output)
                self.assertIn(REJECTION, output)
                self.assertRegex(output, r"file_zephyr|components/file/ports/zephyr")

    def test_the_rejection_can_fail_against_a_weakened_guard(self):
        original = GUARD.read_text()
        for weakening, transform in WEAKENINGS.items():
            weakened = transform(original)
            self.assertNotEqual(weakened, original, "the weakening changed nothing: " + weakening)
            with tempfile.TemporaryDirectory() as temporary:
                guard = Path(temporary) / "CruxConsumerBoundary.cmake"
                guard.write_text(weakened)
                for name, body in SCENARIOS.items():
                    with self.subTest(weakening=weakening, scenario=name):
                        result, output = self.configure(body, guard)
                        # A rejecting test would fail here: the project is accepted, or the error is
                        # only a status line.
                        self.assertEqual(result.returncode, 0, output)


if __name__ == "__main__":
    unittest.main()
