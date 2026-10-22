"""Build consumers independently and reject native public usage requirements."""
import json
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

    def test_native_threading_requirements_are_rejected(self):
        requirements = [('target_link_libraries', value) for value in
                        ('Threads::Threads', 'pthread', '-pthread', '-lpthread',
                         '/usr/lib/libpthread.so')]
        requirements += [('target_compile_options', '-pthread'),
                         ('target_link_options', '-pthread'),
                         ('target_link_options', 'LINKER:-lpthread'),
                         ('target_compile_options', 'SHELL:-pthread')]
        for command, value in requirements:
            for public in (False, True):
                with self.subTest(command=command, value=value, public=public):
                    body = ('find_package(Threads REQUIRED)\n'
                            'file(WRITE "${CMAKE_BINARY_DIR}/main.c" "int main(void) { return 0; }")\n')
                    if public:
                        body += ('add_library(wrapper INTERFACE)\n'
                                 'add_library(facade INTERFACE)\n'
                                 'target_link_libraries(facade INTERFACE wrapper)\n'
                                 f'{command}(wrapper INTERFACE "{value}")\n'
                                 'crux_assert_portable_usage(facade)\n')
                    else:
                        body += ('add_executable(consumer "${CMAKE_BINARY_DIR}/main.c")\n'
                                 f'{command}(consumer PRIVATE "{value}")\n'
                                 'crux_assert_portable_consumer(consumer)\n')
                    result = self.configure(body)
                    self.assertNotEqual(result.returncode, 0, result.stdout + result.stderr)
                    self.assertIn("native consumer requirement", result.stdout + result.stderr)

    def test_static_provider_native_link_only_is_rejected(self):
        for native in ('zephyr_interface', 'Threads::Threads'):
            with self.subTest(native=native):
                result = self.configure('find_package(Threads REQUIRED)\n'
                                        'file(WRITE "${CMAKE_BINARY_DIR}/provider.c" "int provider(void) { return 0; }")\n'
                                        'add_library(zephyr_interface INTERFACE)\n'
                                        'add_library(provider STATIC "${CMAKE_BINARY_DIR}/provider.c")\n'
                                        f'target_link_libraries(provider PRIVATE {native})\n'
                                        'add_library(facade INTERFACE)\n'
                                        'target_link_libraries(facade INTERFACE provider)\n'
                                        'crux_assert_portable_usage(facade)\n')
                self.assertNotEqual(result.returncode, 0, result.stdout + result.stderr)
                self.assertIn("native consumer requirement", result.stdout + result.stderr)

    def test_static_provider_private_native_composition_builds_and_runs(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            native_include = root / "native" / "zephyr"
            native_include.mkdir(parents=True)
            (native_include / "detail.h").write_text('#define NATIVE_RESULT 42\n')
            public_include = root / "public"
            public_include.mkdir()
            (public_include / "api.h").write_text('int provider(void);\nint portable_consumer(void);\n')
            (root / "provider.c").write_text(
                '#include "api.h"\n#include <zephyr/detail.h>\n#include <pthread.h>\n'
                '#ifndef CRUX_PROVIDER_NATIVE\n#error missing provider flag\n#endif\n'
                'static void *run(void *value) { return value; }\n'
                'int provider(void) { pthread_t thread; void *out; int value = NATIVE_RESULT;\n'
                'if (pthread_create(&thread, 0, run, &value) || pthread_join(thread, &out)) return 0;\n'
                'return *(int *)out; }\n')
            (root / "consumer.c").write_text(
                '#include "api.h"\n#ifdef CRUX_PROVIDER_NATIVE\n#error leaked provider flag\n#endif\n'
                'int portable_consumer(void) { return provider(); }\n')
            (root / "main.c").write_text(
                '#include "api.h"\nint main(void) { return portable_consumer() == 42 ? 0 : 1; }\n')
            (root / "CMakeLists.txt").write_text(
                'cmake_minimum_required(VERSION 3.28)\nproject(static_composition LANGUAGES C)\n'
                f'include("{REPO}/cmake/CruxConsumerBoundary.cmake")\n'
                'set(CMAKE_EXPORT_COMPILE_COMMANDS ON)\nfind_package(Threads REQUIRED)\n'
                'add_library(api INTERFACE)\n'
                'target_include_directories(api INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/public")\n'
                'add_library(zephyr_interface INTERFACE)\n'
                'target_include_directories(zephyr_interface INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/native")\n'
                'target_compile_definitions(zephyr_interface INTERFACE CRUX_PROVIDER_NATIVE)\n'
                'add_library(provider STATIC provider.c)\n'
                'target_link_libraries(provider PRIVATE api zephyr_interface Threads::Threads)\n'
                'target_compile_options(provider PRIVATE -pthread)\n'
                'add_library(consumer STATIC consumer.c)\n'
                'target_link_libraries(consumer PRIVATE api)\n'
                'crux_assert_portable_usage(api)\ncrux_assert_portable_consumer(consumer)\n'
                '# The native composition independently selects the implementation.\n'
                'add_executable(composition main.c)\n'
                'target_link_libraries(composition PRIVATE api consumer provider)\n')
            build = root / "build"
            for command in ([CMAKE, "-S", str(root), "-B", str(build)],
                            [CMAKE, "--build", str(build)], [str(build / "composition")]):
                result = subprocess.run(command, capture_output=True, text=True)
                self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            commands = json.loads((build / "compile_commands.json").read_text())
            consumer = next(item['command'] for item in commands
                            if Path(item['file']).name == 'consumer.c')
            provider = next(item['command'] for item in commands
                            if Path(item['file']).name == 'provider.c')
            for private in ('/native', 'CRUX_PROVIDER_NATIVE', '-pthread'):
                self.assertNotIn(private, consumer)
                self.assertIn(private, provider)

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
