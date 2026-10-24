"""Run the source boundary checker on isolated, representative source trees."""
from pathlib import Path
import subprocess
import shutil
import sys
import tempfile
import unittest

REPO = Path(__file__).resolve().parents[2]
CHECKER = REPO / "scripts/check-native-interfaces.py"


class NativeInterfaceTest(unittest.TestCase):
    def check_source(self, source, path="components/example/include/crux/example.h"):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            target = root / path
            target.parent.mkdir(parents=True)
            target.write_text(source)
            return subprocess.run([sys.executable, str(CHECKER), "--root", str(root)],
                                  capture_output=True, text=True)

    def test_logging_consumers_reject_native_leakage(self):
        for path in ("tests/logging_consumer/src/runtime.c", "tests/logging_consumer/src/consumer.cpp",
                     "tests/logging_consumer/src/runtime.h", "samples/logging/controller.c"):
            with self.subTest(path=path):
                bad = self.check_source("#include <zephyr/kernel.h>\n", path)
                self.assertEqual(bad.returncode, 1, bad.stdout + bad.stderr)
                good = self.check_source("#include <crux/logging.h>\n#include <stdio.h>\n", path)
                self.assertEqual(good.returncode, 0, good.stdout + good.stderr)

    def test_serial_consumers_reject_native_leakage(self):
        for path in ("tests/serial_consumer/src/runtime.c", "tests/serial_consumer/src/consumer.cpp",
                     "tests/serial_consumer/src/runtime.h", "samples/serial/controller.c"):
            with self.subTest(path=path):
                bad = self.check_source("#include <zephyr/kernel.h>\n", path)
                self.assertEqual(bad.returncode, 1, bad.stdout + bad.stderr)
                good = self.check_source("#include <crux/serial.h>\n#include <crux/monotonic.h>\n", path)
                self.assertEqual(good.returncode, 0, good.stdout + good.stderr)

    def test_messaging_consumers_reject_native_leakage(self):
        for path in ("tests/messaging_consumer/runtime.c", "tests/messaging_consumer/main.cpp",
                     "samples/messaging/controller.c"):
            with self.subTest(path=path):
                bad = self.check_source("#include <zephyr/kernel.h>\n", path)
                self.assertEqual(bad.returncode, 1, bad.stdout + bad.stderr)
                good = self.check_source("#include <crux/queue.h>\n#include <crux/monotonic.h>\n", path)
                self.assertEqual(good.returncode, 0, good.stdout + good.stderr)

    def test_rejects_native_header_families_and_include_spacing(self):
        for header in ("zephyr/kernel.h", "board.h", "fsl_gpio.h", "fsl_device_registers.h",
                       "MIMXRT1062.h", "cmsis_os2.h", "stm32f4xx.h", "nxp/foo.h",
                       "vendor/chip.h", "boards/board.h", "unistd.h", "sys/socket.h",
                       "windows.h", "pthread.h", "arpa/inet.h", "netinet/in.h", "fcntl.h",
                       "semaphore.h", "sched.h", "poll.h"):
            with self.subTest(header=header):
                result = self.check_source(f' # include /* port */ <{header}>\n')
                self.assertEqual(result.returncode, 1, result.stdout + result.stderr)
                self.assertIn("example.h:1:", result.stdout)

    def test_rejects_native_types_api_calls_and_configuration_tokens(self):
        for source in ("k_timeout_t timeout;", "struct k_sem semaphore;", "struct device *dev;",
                       "struct gpio_dt_spec pin;", "FLEXSPI_Type *base;", "k_sem_take(s, t);",
                       "GPIO_PinWrite(base, 0, 1);", "SDK_DelayAtLeastUs(1, 1);",
                       "#if defined(CONFIG_GPIO)\n#endif", "#ifdef __ZEPHYR__\n#endif",
                       "#if defined(BOARD_EVK)\n#endif", "int x = CONFIG_GPIO;",
                       "gpio_pin_set_dt(pin, 1);", "DT_ALIAS(sw0)",
                       "zsock_socket(0, 0, 0);", "net_if_up(iface);", "wdt_feed(dev, 0);",
                       "hwinfo_get_reset_cause(&cause);", "atomic_t flags;",
                       "device_is_ready(dev);", "CLOCK_EnableClock(0);", "I2C_Init(base, cfg);",
                       "SPI_Init(base, cfg);", "UART_WriteBlocking(base, data, size);",
                       "ENET_Init(base, cfg);", "NVIC_EnableIRQ(0);", "LPUART_Type *uart;",
                       "pthread_t thread;", "clock_gettime(0, &now);", "nanosleep(&delay, 0);"):
            with self.subTest(source=source):
                result = self.check_source(source)
                self.assertEqual(result.returncode, 1, result.stdout + result.stderr)

    def test_rejects_direct_paths_into_providers_and_test_controls(self):
        for header in ("../zephyr/private.h", "../host/private.h", "../../tests/control.h",
                       "../ports/linux/detail.h", "../ports/zephyr/detail.h",
                       "../../vendor/chip.h", "../../boards/board.h", "../private/state.h",
                       "../../ports/linux/thread_native_types.h",
                       "../../ports/zephyr/thread_native_types.h",
                       "../../ports/linux/sync_native_types.h",
                       "../../ports/zephyr/sync_native_types.h",
                       "../ports/linux/private.h", "../ports/zephyr/private.h"):
            with self.subTest(header=header):
                result = self.check_source(f'#include "{header}"\n')
                self.assertEqual(result.returncode, 1, result.stdout + result.stderr)

    def test_rejects_direct_paths_into_component_ports(self):
        for header in ("../ports/linux/queue_port.h", "../ports/zephyr/queue_port.h"):
            for path in ("components/example/include/crux/example.h",
                         "components/example/src/example.c",
                         "tests/messaging_consumer/runtime.c"):
                with self.subTest(header=header, path=path):
                    result = self.check_source(f'#include "{header}"\n', path)
                    self.assertEqual(result.returncode, 1, result.stdout + result.stderr)
                    self.assertIn("native/private header", result.stdout)

    def test_public_headers_cannot_include_shared_implementation_headers(self):
        source = '#include "../src/time_port.h"\n'
        result = self.check_source(source)
        self.assertEqual(result.returncode, 1, result.stdout + result.stderr)
        result = self.check_source('#include "time_port.h"\n', "components/time/src/time.c")
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_applies_to_all_designated_portable_roots(self):
        for path in ("components/new/include/crux/new.h", "components/new/src/shared.c",
                     "components/new/src/shared.cxx", "components/new/include/crux/new.hxx",
                     "components/time/src/time_port_memory.c",
                     "components/runtime/src/thread.c", "components/runtime/portable/thread.c",
                     "components/new/runtime_porting/lock.h", "runtime_porting/include/lock.h",
                     "applications/new/src/main.c", "runtime/src/thread.c",
                     "samples/controller/core/task.c", "samples/controller/include/task.h",
                     "samples/time/include/time_command.h", "samples/time/src/time_command.c",
                     "samples/controller/configuration/scenario.c",
                     "tests/configuration_consumer/consumer.c",
                     "tests/configuration_consumer/consumer.cpp",
                     "tests/file_consumer/consumer.c", "tests/file_consumer/consumer.cpp",
                     "tests/file_consumer/consumer.h",
                     "tests/thread_sync_consumer/consumer.c",
                     "tests/thread_sync_consumer/consumer.cpp",
                     "tests/external_application/consumer.c", "tests/external_application/consumer.cpp"):
            with self.subTest(path=path):
                result = self.check_source("k_timeout_t timeout;", path)
                self.assertEqual(result.returncode, 1, result.stdout + result.stderr)

    def test_networking_profile_is_scoped_to_registered_consumers(self):
        source = "#include <sys/socket.h>\n#include <poll.h>\nclock_gettime(CLOCK_MONOTONIC, &now);\n"
        for path in ("samples/networking/client.c", "tests/networking/runtime_port.c",
                     "tests/networking_consumer/cpp_probe.cpp"):
            with self.subTest(path=path):
                result = self.check_source(source, path)
                self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        result = self.check_source(source, "applications/new/client.c")
        self.assertEqual(result.returncode, 1, result.stdout + result.stderr)

    def test_networking_profile_does_not_allow_native_or_general_posix(self):
        for source in ("#include <pthread.h>\npthread_t thread;", "#include <sys/stat.h>",
                       "#include <zephyr/kernel.h>", "k_sleep(1);", "nanosleep(&now, 0);",
                       "pthread_join(t, 0);", "clock_gettime(CLOCK_REALTIME, &now);",
                       "clock_gettime(clock, &now);", "#include <sys/socket.h>\nbind(fd, a, n);"):
            with self.subTest(source=source):
                result = self.check_source(source, "samples/networking/client.c")
                self.assertEqual(result.returncode, 1, result.stdout + result.stderr)

    def test_native_providers_and_compositions_remain_native(self):
        for path in ("components/time/ports/zephyr/backend.c", "components/time/ports/linux/backend.c",
                     "components/time/zephyr/backend.c", "components/time/host/backend.c",
                     "components/runtime/zephyr/thread.c",
                     "components/thread/ports/linux/thread_native.c",
                     "components/thread/ports/zephyr/thread_native.c",
                     "components/sync/ports/linux/sync_native.c",
                     "components/sync/ports/zephyr/sync_native.c",
                     "tests/thread_sync_consumer/native_worker.c",
                     "tests/thread_sync_consumer/zephyr/native_worker.c",
                     "components/queue/ports/linux/queue_port.c",
                     "components/queue/ports/zephyr/queue_port.c",
                     "components/monotonic/ports/linux/monotonic_host.c",
                     "components/monotonic/ports/zephyr/monotonic_zephyr.c",
                     "samples/controller/zephyr/main.c",
                     "components/netif/ports/linux/backend.c",
                     "components/netif/ports/zephyr/backend.c",
                     "components/file/ports/linux/backend.c",
                     "samples/controller/host/main.c", "samples/time/src/main.c"):
            with self.subTest(path=path):
                result = self.check_source("#include <zephyr/kernel.h>\nk_timeout_t timeout;", path)
                self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_iso_c_and_crux_interfaces_remain_portable(self):
        source = """#include <time.h>
#include <errno.h>
#include <math.h>
#include <stdint.h>
#include <crux/time.h>
struct crux_device { int value; };
struct timespec now;
int result = timespec_get(&now, TIME_UTC);
crux_status_t crux_lock(crux_timeout_t timeout);
#if CRUX_FEATURE
#endif
"""
        result = self.check_source(source)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_documentation_and_string_literals_are_not_native_dependencies(self):
        source = '''/* #include <zephyr/kernel.h>
 * k_timeout_t, struct device, CONFIG_GPIO and GPIO_PinWrite are native.
 */
// k_sem_take and #include <fsl_gpio.h>
const char *description = "k_timeout_t CONFIG_GPIO struct device";
const char *escaped = "\\\" k_sem_take";
'''
        result = self.check_source(source)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_line_splicing_cannot_hide_native_include_or_token(self):
        for source in ("#inc\\\nlude <zephyr/kernel.h>\n", "k_time\\\nout_t timeout;\n"):
            with self.subTest(source=source):
                result = self.check_source(source)
                self.assertEqual(result.returncode, 1, result.stdout + result.stderr)

    def test_invalid_root_reports_an_error_instead_of_success(self):
        result = subprocess.run([sys.executable, str(CHECKER), "--root", "/missing/crux-root"],
                                capture_output=True, text=True)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("root", result.stderr.lower())

    def test_dependency_entry_point_checks_tokens_and_allows_iso_c(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            for relative in ("CMakeLists.txt", "zephyr/module.yml", "zephyr/CMakeLists.txt",
                             "components/time/CMakeLists.txt", "components/base/CMakeLists.txt",
                             "components/time/ports/zephyr/CMakeLists.txt",
                             "tests/time/CMakeLists.txt", "scripts/check-dependencies.sh",
                             "scripts/check-native-interfaces.py", "scripts/check-execution-boundaries.py",
                             "scripts/check-networking-dependencies.py",
                             "tests/portable_execution/conformance.c",
                             "tests/execution_runtime/src/runtime_fixture.c",
                             "tests/execution_firmware/src/consumer.c",
                             "tests/execution_firmware/src/main.cpp",
                             "components/thread/include/crux/thread.h",
                             "components/sync/include/crux/sync.h",
                             "tests/thread_sync_consumer/consumer.c",
                             "tests/thread_sync_consumer/consumer.cpp",
                             "samples/networking/client.c", "samples/networking/client.h",
                             "tests/networking_physical/transaction.c",
                             "tests/networking_physical/frame.c", "tests/networking_physical/frame.h",
                             "tests/networking/runtime_port.c", "tests/networking/runtime_port.h",
                             "tests/networking_consumer/consumer.h",
                             "tests/networking_consumer/cpp_probe.cpp",
                             "components/netif/include/crux/netif.h",
                             "tests/networking_consumer/CMakeLists.txt"):
                destination = root / relative
                destination.parent.mkdir(parents=True, exist_ok=True)
                shutil.copy2(REPO / relative, destination)
            for relative in ("components/base/include", "components/time/include",
                             "components/time/src", "samples/controller/core",
                             "samples/controller/include", "samples/time/src"):
                (root / relative).mkdir(parents=True, exist_ok=True)
            probe = root / "samples/time/src/time_command.c"
            subprocess.run(["git", "init", "-q", str(root)], check=True, capture_output=True)
            for source, expected in (("k_timeout_t timeout;", 1),
                                     ("#include <time.h>\n#include <errno.h>\n", 0)):
                with self.subTest(source=source):
                    probe.write_text(source)
                    result = subprocess.run(["bash", str(root / "scripts/check-dependencies.sh")],
                                            cwd=root, capture_output=True, text=True)
                    self.assertEqual(result.returncode, expected, result.stdout + result.stderr)
