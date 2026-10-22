"""Build consumers independently and reject native public usage requirements."""
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

REPO = Path(__file__).resolve().parents[2]
CMAKE = shutil.which("cmake")


@unittest.skipUnless(CMAKE, "CMake is required")
class ConsumerBoundaryTest(unittest.TestCase):
    def configure(self, body):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            (root / "CMakeLists.txt").write_text(
                'cmake_minimum_required(VERSION 3.28)\nproject(probe LANGUAGES C)\n'
                f'include("{REPO}/cmake/CruxConsumerBoundary.cmake")\n' + body)
            return subprocess.run([CMAKE, "-S", str(root), "-B", str(root / "build")],
                                  capture_output=True, text=True)

    def test_independent_c_and_cpp_consumers(self):
        with tempfile.TemporaryDirectory() as temporary:
            subprocess.run([CMAKE, "-S", str(REPO / "tests/consumer_boundary"),
                            "-B", temporary, f"-DCRUX_SOURCE_DIR={REPO}"], check=True,
                           capture_output=True)
            subprocess.run([CMAKE, "--build", temporary], check=True, capture_output=True)
            for name in ("consumer_c", "consumer_cpp"):
                subprocess.run([str(Path(temporary) / name)], check=True)

    def test_transitive_native_requirements_are_rejected(self):
        for edge in ('zephyr_interface', '$<BUILD_INTERFACE:zephyr_interface>',
                     '$<$<BOOL:1>:zephyr_interface>',
                     '$<IF:$<CONFIG:Debug>,zephyr_interface,portable>',
                     '$<LINK_ONLY:zephyr_interface>',
                     '$<TARGET_NAME_IF_EXISTS:zephyr_interface>', '-lzephyr',
                     '/sdk/lib/libzephyr.a'):
            with self.subTest(edge=edge):
                result = self.configure('add_library(zephyr_interface INTERFACE)\n'
                                        'add_library(portable INTERFACE)\n'
                                        'add_library(wrapper INTERFACE)\n'
                                        f'target_link_libraries(wrapper INTERFACE "{edge}")\n'
                                        'crux_assert_portable_usage(wrapper)\n')
                self.assertNotEqual(result.returncode, 0)
                self.assertIn("native consumer requirement", result.stdout + result.stderr)

    def test_direct_link_injection_is_rejected(self):
        result = self.configure('add_library(zephyr_interface INTERFACE)\n'
                                'add_library(wrapper INTERFACE)\n'
                                'set_property(TARGET wrapper PROPERTY INTERFACE_LINK_LIBRARIES_DIRECT zephyr_interface)\n'
                                'crux_assert_portable_usage(wrapper)\n')
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("native consumer requirement", result.stdout + result.stderr)

    def test_transitive_private_or_native_include_is_rejected(self):
        for path in ('/sdk/zephyr/include', '/sdk/modules/hal/nxp/include',
                     f'{REPO}/components/time/src'):
            with self.subTest(path=path):
                result = self.configure('add_library(provider INTERFACE)\n'
                                        f'target_include_directories(provider INTERFACE "{path}")\n'
                                        'add_library(wrapper INTERFACE)\n'
                                        'target_link_libraries(wrapper INTERFACE provider)\n'
                                        'crux_assert_portable_usage(wrapper)\n')
                self.assertNotEqual(result.returncode, 0)
                self.assertIn("native consumer requirement", result.stdout + result.stderr)

    def test_opaque_generator_expression_is_rejected(self):
        result = self.configure('add_library(wrapper INTERFACE)\n'
                                'target_link_libraries(wrapper INTERFACE "$<TARGET_PROPERTY:other,LINK_LIBRARIES>")\n'
                                'crux_assert_portable_usage(wrapper)\n')
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("unsupported consumer expression", result.stdout + result.stderr)

    def test_private_native_requirements_on_provider_are_allowed(self):
        result = self.configure('file(WRITE "${CMAKE_BINARY_DIR}/provider.c" "int provider(void) { return 0; }")\n'
                                'add_library(zephyr_interface INTERFACE)\n'
                                'add_library(provider SHARED "${CMAKE_BINARY_DIR}/provider.c")\n'
                                'target_link_libraries(provider PRIVATE zephyr_interface)\n'
                                'target_include_directories(provider PRIVATE /sdk/zephyr/include)\n'
                                'add_library(wrapper INTERFACE)\n'
                                'target_link_libraries(wrapper INTERFACE provider)\n'
                                'crux_assert_portable_usage(wrapper)\n')
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_consumer_private_native_requirements_are_rejected(self):
        for requirement in ('target_link_libraries(consumer PRIVATE zephyr_interface)',
                            'target_link_libraries(consumer PRIVATE nxp_sdk)',
                            'target_include_directories(consumer PRIVATE /sdk/zephyr/include)'):
            with self.subTest(requirement=requirement):
                result = self.configure('file(WRITE "${CMAKE_BINARY_DIR}/main.c" "int main(void) { return 0; }")\n'
                                        'add_library(zephyr_interface INTERFACE)\n'
                                        'add_library(nxp_sdk INTERFACE)\n'
                                        'add_executable(consumer "${CMAKE_BINARY_DIR}/main.c")\n'
                                        + requirement + '\ncrux_assert_portable_consumer(consumer)\n')
                self.assertNotEqual(result.returncode, 0)
                self.assertIn("native consumer requirement", result.stdout + result.stderr)

    def test_real_zephyr_facade_has_no_native_transitives(self):
        result = self.configure(f'include("{REPO}/tests/consumer_boundary/mock_zephyr.cmake")\n'
                                f'add_subdirectory("{REPO}/zephyr" zephyr-module)\n'
                                'crux_assert_portable_usage(crux::base crux::time)\n'
                                'get_property(libraries GLOBAL PROPERTY ZEPHYR_LIBS)\n'
                                'if(NOT "crux_time_common" IN_LIST libraries OR NOT "crux_time_zephyr" IN_LIST libraries)\n'
                                '  message(FATAL_ERROR "missing native image registration")\nendif()\n')
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_zephyr_facade_preserves_backend_conflict(self):
        result = self.configure(f'include("{REPO}/tests/consumer_boundary/mock_zephyr.cmake")\n'
                                f'add_subdirectory("{REPO}/zephyr" zephyr-module)\n'
                                'add_library(other_backend INTERFACE)\n'
                                'set_property(TARGET other_backend PROPERTY INTERFACE_CRUX_TIME_BACKEND host)\n'
                                'set_property(TARGET other_backend APPEND PROPERTY COMPATIBLE_INTERFACE_STRING CRUX_TIME_BACKEND)\n'
                                'file(WRITE "${CMAKE_BINARY_DIR}/main.c" "int main(void) { return 0; }")\n'
                                'add_executable(consumer "${CMAKE_BINARY_DIR}/main.c")\n'
                                'target_link_libraries(consumer PRIVATE crux::time other_backend)\n')
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("INTERFACE_CRUX_TIME_BACKEND", result.stdout + result.stderr)
