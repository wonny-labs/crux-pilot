"""Exercise Time's optional Busy-wait through real external C17/C++17 consumers."""
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

REPO = Path(__file__).resolve().parents[2]

CONSUMER = r'''
#include <crux/time.h>
#include <crux/time_authority.h>
#include <crux/time_calendar.h>
#include <assert.h>
int main(void) {
    crux_monotonic_time_t now, deadline;
    crux_duration_t duration;
    assert(crux_duration_try_from_ms(1, &duration) == CRUX_OK);
    assert(crux_monotonic_now(&now) == CRUX_OK);
    assert(crux_monotonic_deadline_after(duration, &deadline) == CRUX_OK);
    assert(crux_monotonic_is_before(now, deadline));
    assert(crux_monotonic_add(now, duration, &deadline) == CRUX_OK);
    crux_datetime_t datetime = {2000, 1, 1, 0, 0, 0, 123};
    crux_realtime_t time;
    assert(crux_realtime_from_utc(&datetime, &time) == CRUX_OK);
    assert(time.seconds == 946684800 && time.nanoseconds == 123);
    assert(crux_realtime_to_utc(time, &datetime) == CRUX_OK && datetime.year == 2000);
    assert(crux_realtime_init() == CRUX_OK);
    crux_realtime_snapshot_t snapshot;
    assert(crux_realtime_get_snapshot(&snapshot) == CRUX_OK);
    assert(!snapshot.is_valid && !snapshot.is_synchronized);
    assert(snapshot.last_source == CRUX_REALTIME_SOURCE_NONE);
    assert(crux_realtime_now(&time) == CRUX_ERR_NO_DATA);
    assert(crux_realtime_set_synchronized(false) == CRUX_ERR_NO_DATA);
    assert(crux_realtime_invalidate() == CRUX_OK);
    assert(crux_busy_wait(crux_duration_from_ns(-1)) == CRUX_ERR_INVALID_ARGUMENT);
    assert(crux_busy_wait(crux_duration_from_ns(0)) == CRUX_OK);
    assert(crux_busy_wait(crux_duration_from_ns(1)) == EXPECT_WAIT);
    return 0;
}
'''


class UnifiedTimeConsumer(unittest.TestCase):
    def setUp(self):
        if not shutil.which('cmake'):
            self.skipTest('CMake is unavailable')

    def configure(self, provider='memory', enabled=False, extra=()):
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        root = Path(temporary.name)
        (root / 'consumer.c').write_text(CONSUMER)
        (root / 'consumer.cpp').write_text(CONSUMER)
        (root / 'CMakeLists.txt').write_text('''
cmake_minimum_required(VERSION 3.28)
project(time_consumer LANGUAGES C CXX)
set(CMAKE_C_STANDARD 17)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_C_EXTENSIONS OFF)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CRUX_BUILD_TIME ON CACHE BOOL "")
add_subdirectory("''' + str(REPO) + '''" crux)
if(TARGET crux::delay OR TARGET crux::interrupt OR TARGET crux_thread_host OR TARGET crux_sync_host)
  message(FATAL_ERROR "Unrelated public capability/provider was configured")
endif()
foreach(language IN ITEMS c cpp)
  add_executable(consumer_${language} consumer.${language})
  target_link_libraries(consumer_${language} PRIVATE crux::time)
  target_compile_definitions(consumer_${language} PRIVATE EXPECT_WAIT=''' +
            ('CRUX_OK' if enabled else 'CRUX_ERR_NOT_SUPPORTED') + ''')
  target_compile_options(consumer_${language} PRIVATE -Wall -Wextra -Wpedantic -Werror)
  crux_assert_portable_consumer(consumer_${language})
endforeach()
''')
        build = root / 'build'
        result = subprocess.run(['cmake', '-S', str(root), '-B', str(build),
                                 f'-DCRUX_TIME_PROVIDER={provider}',
                                 *(['-DCRUX_TIME_BUSY_WAIT=' + ('ON' if enabled else 'OFF')] if enabled is not None else []),
                                 *extra], capture_output=True, text=True)
        return build, result

    def test_public_time_consumers_with_optional_native_wait(self):
        for provider in ('memory', 'linux'):
            for enabled in (False, True):
                with self.subTest(provider=provider, enabled=enabled):
                    build, result = self.configure(provider, enabled)
                    self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                    result = subprocess.run(['cmake', '--build', str(build), '-j2'],
                                            capture_output=True, text=True)
                    self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                    for language in ('c', 'cpp'):
                        result = subprocess.run([str(build / ('consumer_' + language))],
                                                capture_output=True, text=True, timeout=10)
                        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                    if not enabled:
                        self.assertFalse(list(build.rglob('*busy_wait_linux*')),
                                         'Disabled feature built its native provider')

    def test_obsolete_delay_selection_is_rejected_even_when_off(self):
        for value in ('ON', 'OFF'):
            with self.subTest(value=value):
                _, result = self.configure(extra=(f'-DCRUX_BUILD_DELAY={value}',))
                self.assertNotEqual(result.returncode, 0)
                self.assertIn('CRUX_TIME_BUSY_WAIT', result.stdout + result.stderr)

    def test_busy_wait_cannot_enable_without_time(self):
        _, result = self.configure(enabled=True, extra=('-DCRUX_BUILD_TIME=OFF',))
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('CRUX_TIME_BUSY_WAIT requires CRUX_BUILD_TIME', result.stdout + result.stderr)

    def test_external_default_disables_busy_wait(self):
        build, result = self.configure(enabled=None)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        result = subprocess.run(['cmake', '--build', str(build), '-j2'],
                                capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        for language in ('c', 'cpp'):
            result = subprocess.run([str(build / ('consumer_' + language))],
                                    capture_output=True, text=True, timeout=10)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertFalse(list(build.rglob('*busy_wait_linux*')))

class PrivateTimingContracts(unittest.TestCase):
    def test_linux_enabled_busy_wait_rejects_32bit_time_t(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            (root / 'time.h').write_text(
                '#include <stdint.h>\ntypedef int32_t time_t;\n'
                'struct timespec { time_t tv_sec; long tv_nsec; };\n'
                '#define CLOCK_MONOTONIC 1\n'
                'int clock_gettime(int, struct timespec *);\n'
                'int clock_getres(int, struct timespec *);\n')
            result = subprocess.run([
                'cc', '-std=c17', '-fsyntax-only', '-I', str(root),
                '-I', str(REPO / 'components/base/include'),
                '-I', str(REPO / 'components/time/src'),
                str(REPO / 'components/time/ports/linux/busy_wait_linux.c')],
                capture_output=True, text=True)
            self.assertNotEqual(result.returncode, 0, 'Unsupported enabled provider compiled')
            self.assertIn('64-bit time_t', result.stdout + result.stderr)

    def test_enabled_unsupported_zephyr_profiles_fail_configuration(self):
        profiles = ('CONFIG_SMP', 'CONFIG_TIMER_READS_ITS_FREQUENCY_AT_RUNTIME',
                    'CONFIG_SYSTEM_CLOCK_HW_CYCLES_PER_SEC_RUNTIME_UPDATE',
                    'CONFIG_ARCH_HAS_CUSTOM_BUSY_WAIT')
        for profile in profiles:
            with self.subTest(profile=profile), tempfile.TemporaryDirectory() as temporary:
                root = Path(temporary)
                (root / 'CMakeLists.txt').write_text(
                    'cmake_minimum_required(VERSION 3.28)\nproject(profile LANGUAGES C)\n'
                    f'include("{REPO}/tests/consumer_boundary/mock_zephyr.cmake")\n'
                    'set(CONFIG_SYS_CLOCK_EXISTS ON)\nset(CONFIG_CRUX_TIME_BUSY_WAIT ON)\n'
                    f'set({profile} ON)\nadd_subdirectory("{REPO}/zephyr" crux)\n')
                result = subprocess.run(['cmake', '-S', str(root), '-B', str(root / 'build')],
                                        capture_output=True, text=True)
                self.assertNotEqual(result.returncode, 0)
                diagnostic = ' '.join((result.stdout + result.stderr).split())
                self.assertIn('validated stable single-core timer profile', diagnostic)

    def compile_and_run(self, body, sources, includes=(), definitions=()):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            source = root / 'probe.c'
            source.write_text(body)
            shim = root / 'zephyr'
            shim.mkdir()
            (shim / 'kernel.h').write_text(
                '#include <stdint.h>\nint64_t sample_ticks(void);\n'
                '#define k_uptime_ticks sample_ticks\n')
            command = ['cc', '-std=c17', '-Wall', '-Wextra', '-Werror',
                       '-I', str(root), '-I', str(REPO / 'components/base/include'),
                       '-I', str(REPO / 'components/time/include'),
                       '-I', str(REPO / 'components/time/src'),
                       '-I', str(REPO / 'components/interrupt/src')]
            for value in includes:
                command += ['-I', str(REPO / value)]
            command += list(definitions)
            result = subprocess.run([*command, str(source),
                                     *(str(REPO / s) for s in sources), '-o', str(root / 'probe')],
                                    capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            result = subprocess.run([str(root / 'probe')], capture_output=True, text=True, timeout=5)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_native_realtime_lock_rejects_isr_before_mutex(self):
        self.compile_and_run(r'''
#include "realtime_port.h"
#include "interrupt.h"
#include <pthread.h>
#include <assert.h>
static _Thread_local bool in_isr;
static int locks;
bool crux_is_in_isr(void) { return in_isr; }
int pthread_mutex_lock(pthread_mutex_t *mutex) { (void)mutex; ++locks; return 0; }
int pthread_mutex_unlock(pthread_mutex_t *mutex) { (void)mutex; return 0; }
int main(void) {
    in_isr = true;
    assert(crux_realtime_impl_lock() == CRUX_ERR_NOT_SUPPORTED && locks == 0);
    in_isr = false;
    assert(crux_realtime_impl_lock() == CRUX_OK && locks == 1);
    crux_realtime_impl_unlock();
    return 0;
}
''', ['components/time/ports/linux/time_lock_linux.c'])

    def test_noninteger_tick_upper_bound_uses_one_next_tick_sample(self):
        self.compile_and_run(r'''
#include "monotonic_port.h"
#include <assert.h>
static int samples;
bool crux_is_in_isr(void) { return false; }
int64_t sample_ticks(void) { ++samples; return 1; }
int main(void) {
    int64_t out = -1;
    assert(crux_monotonic_impl_read_upper_ns(&out) == CRUX_OK);
    assert(out == 666666667 && samples == 1);
    assert(crux_monotonic_impl_read_ns(&out) == CRUX_OK);
    assert(out == 333333333 && samples == 2);
    return 0;
}
''', ['components/time/ports/zephyr/time_clock_zephyr.c',
      'components/time/src/time_clock.c', 'components/time/src/time_types.c'],
            definitions=('-DCONFIG_SYS_CLOCK_TICKS_PER_SEC=3',))

    def test_disabled_wait_needs_no_native_clock_or_provider(self):
        self.compile_and_run(r'''
#include <crux/time.h>
#include <assert.h>
int main(void) {
    assert(crux_busy_wait(crux_duration_from_ns(-1)) == CRUX_ERR_INVALID_ARGUMENT);
    assert(crux_busy_wait(crux_duration_from_ns(0)) == CRUX_OK);
    assert(crux_busy_wait(crux_duration_from_ns(1)) == CRUX_ERR_NOT_SUPPORTED);
    assert(crux_busy_wait(crux_duration_from_ns(INT64_MAX)) == CRUX_ERR_NOT_SUPPORTED);
    return 0;
}
''', ['components/time/src/busy_wait.c', 'components/time/src/busy_wait_disabled.c'])


if __name__ == "__main__":
    unittest.main()
