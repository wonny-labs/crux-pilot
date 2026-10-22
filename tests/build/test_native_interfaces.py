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
                       "../../vendor/chip.h", "../../boards/board.h", "../private/state.h"):
            with self.subTest(header=header):
                result = self.check_source(f'#include "{header}"\n')
                self.assertEqual(result.returncode, 1, result.stdout + result.stderr)

    def test_public_headers_cannot_include_shared_implementation_headers(self):
        source = '#include "../src/time_port.h"\n'
        result = self.check_source(source)
        self.assertEqual(result.returncode, 1, result.stdout + result.stderr)
        result = self.check_source('#include "time_port.h"\n', "components/time/src/time.c")
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_applies_to_all_designated_portable_roots(self):
        for path in ("components/new/include/crux/new.h", "components/new/src/shared.c",
                     "components/new/src/shared.cxx", "components/new/include/crux/new.hxx",
                     "components/runtime/src/thread.c", "components/runtime/portable/thread.c",
                     "components/new/runtime_porting/lock.h", "runtime_porting/include/lock.h",
                     "applications/new/src/main.c", "runtime/src/thread.c",
                     "samples/controller/core/task.c", "samples/controller/include/task.h",
                     "samples/time/include/time_command.h", "samples/time/src/time_command.c"):
            with self.subTest(path=path):
                result = self.check_source("k_timeout_t timeout;", path)
                self.assertEqual(result.returncode, 1, result.stdout + result.stderr)

    def test_native_providers_and_compositions_remain_native(self):
        for path in ("components/time/zephyr/backend.c", "components/time/host/backend.c",
                     "components/runtime/zephyr/thread.c", "samples/controller/zephyr/main.c",
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
                             "tests/time/CMakeLists.txt", "scripts/check-dependencies.sh",
                             "scripts/check-native-interfaces.py"):
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
