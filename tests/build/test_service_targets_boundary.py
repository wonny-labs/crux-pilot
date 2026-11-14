"""The consumer guard accepts the portable service targets and rejects every way past them (ADR-0004, T1).

`crux::access`, `crux::credential_store` and `crux::security_event` are portable usage targets, not
composition interfaces: a portable consumer may link them, and the guard accepts them. What the guard
must still refuse is a consumer that reaches the private adapter headers, or the native provider, through
them. The checks configure small CMake projects on the host with the real guards (cmake/Crux*Boundary.cmake)
and the real definitions of the targets, taken from components/*/ports/zephyr/CMakeLists.txt, without a
Zephyr SDK. The same projects configure cleanly against weakened copies of the guards, so a passing
rejection is not vacuous.

This differs from the File test on purpose. `crux::file_zephyr` is a composition target that the guard
rejects. These three are portable, so the question here is the opposite one: is the target accepted, and
can nothing private be reached through it?
"""
from pathlib import Path
import re
import shutil
import subprocess
import tempfile
import unittest

REPO = Path(__file__).resolve().parents[2]
CMAKE = shutil.which("cmake")
GUARDS = {name: REPO / "cmake" / name for name in
          ("CruxConsumerBoundary.cmake", "CruxAccessBoundary.cmake", "CruxCredentialBoundary.cmake")}
COMPONENTS = REPO / "components"

FUNCTIONS = {}
for _component, _function in (("access", "crux_access_public_target"),
                              ("credential", "crux_credential_public_target"),
                              ("security_event", "crux_security_event_public_target")):
    _text = (COMPONENTS / _component / "ports/zephyr/CMakeLists.txt").read_text()
    _match = re.search(rf"function\({_function}\).*?endfunction\(\)", _text, re.S)
    FUNCTIONS[_component] = _match.group(0) if _match else ""

# The mock needs only what the real functions read: the component directories and the two contracts
# that the targets link. Each function text is the real one, so a change to what a target exposes
# changes this test's input.
PROJECT = f"""cmake_minimum_required(VERSION 3.28)
project(probe LANGUAGES C)
include("@GUARDS@/CruxConsumerBoundary.cmake")
include("@GUARDS@/CruxAccessBoundary.cmake")
include("@GUARDS@/CruxCredentialBoundary.cmake")
set(CRUX_ACCESS_COMPONENT_DIR "{COMPONENTS}/access")
set(CRUX_CREDENTIAL_COMPONENT_DIR "{COMPONENTS}/credential")
set(CRUX_SECURITY_EVENT_COMPONENT_DIR "{COMPONENTS}/security_event")
add_library(crux_base INTERFACE)
target_include_directories(crux_base INTERFACE "{COMPONENTS}/base/include")
add_library(crux::base ALIAS crux_base)
add_library(crux_file INTERFACE)
target_include_directories(crux_file INTERFACE "{COMPONENTS}/file/include")
add_library(crux::file ALIAS crux_file)
{FUNCTIONS["access"]}
{FUNCTIONS["credential"]}
{FUNCTIONS["security_event"]}
crux_access_public_target()
crux_credential_public_target()
crux_security_event_public_target()
file(WRITE "${{CMAKE_BINARY_DIR}}/main.c" "int main(void) {{ return 0; }}\\n")
"""

TARGETS = ("crux::access", "crux::credential_store", "crux::security_event")

PORTABLE_CONSUMER = """add_executable(app "${CMAKE_BINARY_DIR}/main.c")
target_link_libraries(app PRIVATE crux::access crux::credential_store crux::security_event)
crux_assert_portable_consumer(app)
crux_assert_portable_usage(crux::access crux::credential_store crux::security_event)
crux_assert_access_adapter_unreachable(crux::access crux::credential_store crux::security_event)
crux_assert_credential_adapter_unreachable(crux::access crux::credential_store crux::security_event)
crux_assert_access_consumer(app)
crux_assert_credential_consumer(app)
"""

# What a consumer might do to get past a portable target, and the rule that must refuse it.
ESCAPES = {
    "the Access adapter directory on the include path": (
        f"""add_executable(app "${{CMAKE_BINARY_DIR}}/main.c")
target_link_libraries(app PRIVATE crux::access)
target_include_directories(app PRIVATE "{COMPONENTS}/access/adapter")
crux_assert_access_consumer(app)
""", "Access adapter header reachable", "CruxAccessBoundary.cmake"),
    "the credential adapter directory on the include path": (
        f"""add_executable(app "${{CMAKE_BINARY_DIR}}/main.c")
target_link_libraries(app PRIVATE crux::credential_store)
target_include_directories(app PRIVATE "{COMPONENTS}/credential/adapter")
crux_assert_credential_consumer(app)
""", "Credential adapter header reachable", "CruxCredentialBoundary.cmake"),
    "a wrapper that publishes the credential adapter directory": (
        f"""add_library(product_credentials INTERFACE)
target_link_libraries(product_credentials INTERFACE crux::credential_store)
target_include_directories(product_credentials INTERFACE "{COMPONENTS}/credential/adapter")
add_executable(app "${{CMAKE_BINARY_DIR}}/main.c")
target_link_libraries(app PRIVATE product_credentials)
crux_assert_credential_adapter_unreachable(product_credentials)
""", "Credential adapter header reachable", "CruxCredentialBoundary.cmake"),
    "the native provider library through a portable wrapper": (
        """add_library(zephyr_interface INTERFACE)
add_library(product_services INTERFACE)
target_link_libraries(product_services INTERFACE crux::access zephyr_interface)
crux_assert_portable_usage(product_services)
""", "native consumer requirement", "CruxConsumerBoundary.cmake"),
    "a provider source directory on the include path": (
        f"""add_library(product_services INTERFACE)
target_link_libraries(product_services INTERFACE crux::security_event)
target_include_directories(product_services INTERFACE "{COMPONENTS}/security_event/src")
crux_assert_portable_usage(product_services)
""", "native consumer requirement", "CruxConsumerBoundary.cmake"),
}


def demote(text):
    return re.sub(r'message\(FATAL_ERROR "(native consumer requirement|Access adapter header reachable'
                  r'|Credential adapter header reachable)', r'message(STATUS "\1', text)


@unittest.skipUnless(CMAKE and all(FUNCTIONS.values()), "CMake and the target definitions are required")
class PortableTargetsAreAcceptedAndNothingPrivateIsReachable(unittest.TestCase):
    def configure(self, body, guards=None, weaken=None):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            directory = guards or REPO / "cmake"
            if weaken is not None:
                directory = root / "guards"
                directory.mkdir()
                for name, path in GUARDS.items():
                    text = path.read_text()
                    if name == weaken:
                        changed = demote(text)
                        self.assertNotEqual(changed, text, "the weakening changed nothing: " + name)
                        text = changed
                    # The wrappers include their sibling by this directory, so keep the names.
                    (directory / name).write_text(text)
            (root / "CMakeLists.txt").write_text(PROJECT.replace("@GUARDS@", str(directory)) + body)
            result = subprocess.run([CMAKE, "-S", str(root), "-B", str(root / "build")],
                                    capture_output=True, text=True)
        return result, result.stdout + result.stderr

    def test_the_mock_carries_the_real_definitions(self):
        result, output = self.configure("")
        self.assertEqual(result.returncode, 0, output)
        for component, text in FUNCTIONS.items():
            self.assertIn("/include\"", text, component)

    def test_a_portable_consumer_of_all_three_passes_every_check(self):
        result, output = self.configure(PORTABLE_CONSUMER)
        self.assertEqual(result.returncode, 0, output)
        for rejection in ("native consumer requirement", "adapter header reachable"):
            self.assertNotIn(rejection, output)

    def test_each_portable_target_passes_the_checks_alone(self):
        for target in TARGETS:
            with self.subTest(target=target):
                body = (f"crux_assert_portable_usage({target})\n"
                        f"crux_assert_access_adapter_unreachable({target})\n"
                        f"crux_assert_credential_adapter_unreachable({target})\n")
                result, output = self.configure(body)
                self.assertEqual(result.returncode, 0, output)

    def test_every_way_past_a_portable_target_is_rejected(self):
        for name, (body, message, _guard) in ESCAPES.items():
            with self.subTest(escape=name):
                result, output = self.configure(body)
                self.assertNotEqual(result.returncode, 0, output)
                self.assertIn(message, output)

    def test_each_escape_passes_against_the_guard_that_it_needs_weakened(self):
        for name, (body, _message, guard) in ESCAPES.items():
            with self.subTest(escape=name, weakened=guard):
                result, output = self.configure(body, weaken=guard)
                # A rejecting test would fail here: the project is accepted once its guard is
                # reduced to a status line.
                self.assertEqual(result.returncode, 0, output)

    def test_the_guard_rejects_a_target_definition_that_exposes_a_private_directory(self):
        # The same project with the credential target changed to carry the adapter directory: the
        # acceptance of the real definition above is not vacuous, because the guard sees this one.
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            exposed = ("crux_credential_public_target()\n"
                       "target_include_directories(crux_credential_store INTERFACE "
                       f'"{COMPONENTS}/credential/adapter")\n')
            text = PROJECT.replace("crux_credential_public_target()\n", exposed, 1)
            (root / "CMakeLists.txt").write_text(
                text.replace("@GUARDS@", str(REPO / "cmake")) +
                "crux_assert_credential_adapter_unreachable(crux::credential_store)\n")
            result = subprocess.run([CMAKE, "-S", str(root), "-B", str(root / "build")],
                                    capture_output=True, text=True)
        output = result.stdout + result.stderr
        self.assertNotEqual(result.returncode, 0, output)
        self.assertIn("Credential adapter header reachable", output)


if __name__ == "__main__":
    unittest.main()
